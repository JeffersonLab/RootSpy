// RSTimeSeries
//
// Program to periodically run macros that write values
// to the time series DB. The location of the DB is
// set by the following environment variables:
//
//    HDTSDB_HOST    (def. halldweb)
//    HDTSDB_PORT    (def. 8086    )
//    HDTSDB_DB      (def. RootSpy )
//

#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include <TROOT.h>
#include <TFile.h>
#include <TDirectory.h>
#include <TObjArray.h>
#include <TH1F.h>
#include <TClass.h>
#include <TTree.h> 
#include <TList.h>
#include <TStyle.h>
#include <TInterpreter.h>
#include <TLatex.h>
#include <TPad.h>

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <mutex>
using namespace std;

#include "RSTimeSeries.h"

#include "rs_cmsg.h"
#include "rs_info.h"
#include "rs_macroutils.h"
#include "rs_influxdb.h"

//////////////////////////////////////////////
// GLOBALS
//////////////////////////////////////////////
rs_cmsg *RS_CMSG = NULL;
rs_xmsg *RS_XMSG = NULL;
rs_info *RS_INFO = NULL;
pthread_rwlock_t *ROOT_MUTEX = NULL;
bool MAKE_BACKUP = false;
set<string> MACRO_HNAMEPATHS;

bool USE_CMSG=false;
bool USE_XMSG=true;

// These defined in rs_cmsg.cc
extern mutex REGISTRATION_MUTEX_CMSG;
extern map<void*, string> HISTOS_TO_REGISTER_CMSG;
extern map<void*, string> MACROS_TO_REGISTER_CMSG;

// These defined in rs_xmsg.cc
extern mutex REGISTRATION_MUTEX_XMSG;
extern set<rs_serialized*> HISTOS_TO_REGISTER_XMSG;
extern set<rs_serialized*> MACROS_TO_REGISTER_XMSG;


static int VERBOSE = 1;  // Verbose output to screen - default is to print out some information

// configuration variables
namespace config {
	static string ROOTSPY_UDL = "cMsg://127.0.0.1/cMsg/rootspy";
	static string CMSG_NAME = "<not set here. see below>";
	static string SESSION = "";
		
	static double POLL_DELAY = 60;   // time between polling runs
	static double MIN_POLL_DELAY = 10;
	
	// run management 
	//bool KEEP_RUNNING = true;
	static bool FORCE_START = false;
	
	bool DONE = false;
	bool RUN_IN_PROGRESS = false;
	int RUN_NUMBER = 99999;   
	
}

using namespace config;


//////////////////////////////////////////////
// FUNCTIONS
//////////////////////////////////////////////

void BeginRun();
void MainLoop(void);
void GetAllHists(uint32_t Twait=2); // Twait is in seconds
void ExecuteMacro(TDirectory *f, string macro);
void ParseCommandLineArguments(int &narg, char *argv[]);
void Usage(void);

void signal_stop_handler(int signum);

//-----------
// signal_stop_handler
//-----------
void signal_stop_handler(int signum)
{
	cerr << "received signal " << signum << "..." << endl;
	
	// let main loop know that it's time to stop
	DONE = true;
}

//-------------------
// main
//-------------------
int main(int narg, char *argv[])
{
	// Parse the command line arguments
	ParseCommandLineArguments(narg, argv);
	
	// register signal handlers to properly stop running
	if(signal(SIGHUP, signal_stop_handler)==SIG_ERR)
	cerr << "unable to set HUP signal handler" << endl;
	if(signal(SIGINT, signal_stop_handler)==SIG_ERR)
	cerr << "unable to set INT signal handler" << endl;
	
	// when running curl as external process, suppress use
	// of proxy which will cause failure to connect to influxdb
	putenv((char*)"HTTP_PROXY=");
	putenv((char*)"HTTPS_PROXY=");
	putenv((char*)"http_proxy=");
	putenv((char*)"https_proxy=");

	// ------------------- initialization ---------------------------------
	BeginRun();
	
	// ------------------- periodic processing ----------------------------
	MainLoop();  // regularly poll servers for new histograms
	

	if( RS_CMSG ) delete RS_CMSG;
	if( RS_XMSG ) delete RS_XMSG;
	
	return 0;
}


//-----------
// BeginRun
//-----------
void BeginRun()
{
	// Create rs_info object
	RS_INFO = new rs_info();
	
	// Makes a Mutex to lock / unlock Root Global
	ROOT_MUTEX = new pthread_rwlock_t;
	pthread_rwlock_init(ROOT_MUTEX, NULL);
	
	// ------------------- cMsg initialization ----------------------------
	// Create cMsg object
	char hostname[256];
	gethostname(hostname, 256);
	char str[512];
	sprintf(str, "RSTimeSeries_%d", getpid());
	CMSG_NAME = string(str);
	cout << "Full UDL is " << ROOTSPY_UDL << endl;
	if( USE_CMSG ) RS_CMSG = new rs_cmsg(ROOTSPY_UDL, CMSG_NAME);
	if( USE_XMSG ) RS_XMSG = new rs_xmsg(ROOTSPY_UDL, CMSG_NAME);
	
	// set session name to some default
	if(SESSION.empty()) SESSION="halldsession";

	// Setup interpretor so macros don't have to include these things.
	gROOT->ProcessLine("#include <iostream>");
	gROOT->ProcessLine("using namespace std;");
	gROOT->ProcessLine("#define ROOTSPY_MACROS");
	gROOT->ProcessLine("extern \"C\" { void rs_SetFlag(const string flag, int val); }");
	gROOT->ProcessLine("extern \"C\" { int  rs_GetFlag(const string flag); }");
	gROOT->ProcessLine("extern \"C\" { void rs_ResetHisto(const string hnamepath); }");
	gROOT->ProcessLine("extern \"C\" { void rs_RestoreHisto(const string hnamepath); }");
	gROOT->ProcessLine("extern \"C\" { void rs_ResetAllMacroHistos(const string hnamepath); }");
	gROOT->ProcessLine("extern \"C\" { void rs_RestoreAllMacroHistos(const string hnamepath); }");
	gROOT->ProcessLine("extern \"C\" { void rs_SavePad(const string fname, int ipad){} }"); // disable this for RSTimeSeries (it's meant for RSAI)
	gROOT->ProcessLine("extern \"C\" { void InsertSeriesData(string sdata) }");
	gROOT->ProcessLine("extern \"C\" { void InsertSeriesMassFit(string ptype, double mass, double width, double mass_err, double width_err, double unix_time=0.0) }");

	// Set flag so macros will automatically reset histograms after
	// a successful fit.
	rs_SetFlag("Is_RSTimeSeries", 1);
	rs_SetFlag("RESET_AFTER_FIT", 1);
	rs_SetFlag("RESET_AFTER_SAVEPAD", 0); // ensure histograms are not automatically reset by macros supporting RSAI

	// The following ensures that the routines in rs_macroutils are
	// linked in to the final executable so they are available to 
	// the macros.
	if(rs_GetFlag("RESET_AFTER_FIT") != 1){
		// (should never actually get here)
		rs_ResetHisto("N/A");
		rs_RestoreHisto("N/A");
		
		rs_influxdb db("",""); // from rs_influxdb.cc so InsertSeriesData and InsertSeriesMassFit get linked
	}
}

//-----------
// MainLoop
//-----------
void MainLoop(void)
{
	if(VERBOSE>0) _DBG_ << "Running main event loop..." << endl;
	
	// Loop until we are told to stop for some reason	
	while(!DONE) {		    
		// keeps the connections alive, and keeps the list of servers up-to-date
		if( RS_CMSG )RS_CMSG->PingServers();
		if( RS_XMSG )RS_XMSG->PingServers();
		
		if(VERBOSE>1)  _DBG_ << "number of servers = " << RS_INFO->servers.size() << endl;
		
		// Request current set of histograms 
		GetAllHists();
		
		// Run macros
		RS_INFO->Lock();
		for(auto &hnamepath : MACRO_HNAMEPATHS){
			auto hdef = RS_INFO->histdefs[hnamepath];
			if(hdef.hists.empty()) continue;
			auto the_hinfo = hdef.hists.begin()->second;
			if(the_hinfo.Nkeys == 1){
				ExecuteMacro(RS_INFO->sum_dir, the_hinfo.macroString);
			}else{
				ExecuteMacro(the_hinfo.macroData, the_hinfo.macroString);
			}
		}
		RS_INFO->Unlock();
		
		// sleep for awhile
		sleep(POLL_DELAY);
	}
}


//-----------
// GetAllHists
//-----------
void GetAllHists(uint32_t Twait)
{
	/// This will:
	///
	/// 1. Send a request out to all servers for their list of defined
	///    macros.
	///
	/// 2. Wait for 2 seconds for them to respond with their macro lists
	///
	/// 3. Send a request for the macro definitions
	///
	/// 4. Wait 2 seconds for them to respond with their macro definitions
	///
	/// 5. Look through the macro definitions and find ones that contain
	///    the string "InsertSeriesMassFit" or "InsertSeriesData"
	///
	/// 6. Send a request for the MACRO_HNAMEPATHS of all macros selected 
	///    in 5.
	///
	/// 7. Wait Twait seconds for them to respond.
	///
	/// Thus, by the time this routine returns a list of macros that may
	/// insert entries in the time series database will have been obtained
	/// and all histograms those macros need will have been updated from
	/// all servers.
	///
	/// n.b. This routine will take a minimum of Twait+4 seconds to complete!
	
	// 1. Send a request out to all servers for their list of defined macros.
	if(VERBOSE>1) cout << "Requesting macro and histogram lists ..." << endl;
	if( RS_CMSG ) RS_CMSG->RequestMacroList("rootspy");
	if( RS_CMSG ) RS_CMSG->RequestHists("rootspy");
	if( RS_XMSG ) RS_XMSG->RequestMacroList("rootspy");
	if( RS_XMSG ) RS_XMSG->RequestHists("rootspy");
	if(VERBOSE>1) cout << "Waiting 2 seconds for servers to send macro and histogram lists" << endl;
	sleep(2);
	
	// 3. Send a request for the macro definitions
	RS_INFO->Lock();
	for(auto &p : RS_INFO->histdefs){
		hdef_t &hdef = p.second;
		if( hdef.type == hdef_t::macro ){
			if(VERBOSE>1) cout << "Requesting macro " <<  hdef.hnamepath << " ..." << endl;
			if( RS_CMSG ) RS_CMSG->RequestMacro(p.first, hdef.hnamepath);
			if( RS_XMSG ) RS_XMSG->RequestMacro(p.first, hdef.hnamepath);
		}
	}	
	RS_INFO->Unlock();
	if(VERBOSE>1) cout << "Waiting 2 seconds for servers to send macro definitions" << endl;
	sleep(2);
	
	// Register any macros waiting in the queue
	if( ! MACROS_TO_REGISTER_CMSG.empty() ){
		REGISTRATION_MUTEX_CMSG.lock();
		for(auto m : MACROS_TO_REGISTER_CMSG){
			if( RS_CMSG ) RS_CMSG->RegisterMacro(m.second, (cMsgMessage*)m.first);
		}
		MACROS_TO_REGISTER_CMSG.clear();
		REGISTRATION_MUTEX_CMSG.unlock();
	}    
	
	// Register any macros waiting in the queue
	if( ! MACROS_TO_REGISTER_XMSG.empty() ){
		REGISTRATION_MUTEX_XMSG.lock();
		for(auto m : MACROS_TO_REGISTER_XMSG){
			if( RS_XMSG ) RS_XMSG->RegisterMacro(m);
		}
		MACROS_TO_REGISTER_XMSG.clear();
		REGISTRATION_MUTEX_XMSG.unlock();
	}    

	// 5. Look through the macro definitions and find ones that contain
	//    the string "InsertSeriesMassFit" or "InsertSeriesData"
	RS_INFO->Lock();
	for(auto &p : RS_INFO->hinfos){
		string &macroString = p.second.macroString;
		if(macroString.find("InsertSeriesMassFit") != string::npos) MACRO_HNAMEPATHS.insert(p.second.hnamepath);
		if(macroString.find("InsertSeriesData"   ) != string::npos) MACRO_HNAMEPATHS.insert(p.second.hnamepath);
	}
	
	// 6. Send a request for the MACRO_HNAMEPATHS of all macros selected
	vector<string> hnamepaths;
	for(auto &hnamepath : MACRO_HNAMEPATHS){
		auto &hdef = RS_INFO->histdefs[hnamepath];
		if(VERBOSE>1) cout << "Requesting " << hdef.macro_hnamepaths.size() << " histos for " <<  hnamepath << " ..." << endl;
		for(auto h: hdef.macro_hnamepaths) hnamepaths.push_back(h);
	}
	RS_INFO->Unlock();
	if( !hnamepaths.empty() ) {
		if( RS_CMSG ) RS_CMSG->RequestHistograms("rootspy", hnamepaths);
		if( RS_XMSG ) RS_XMSG->RequestHistograms("rootspy", hnamepaths);
	}
	
	// If Twait is "0", then return now.
	if(Twait == 0) return;
	
	// Give servers Twait seconds to respond with histograms
	if(VERBOSE>1) cout << "Waiting "<<Twait<<" seconds for servers to send histograms" << endl;
	sleep(Twait);
	
	// The RootSpy GUI requires all macros and histograms be
	// processed in the main ROOT GUI thread to avoid crashes.
	// Thus, when messages with histgrams come in they are
	// stored in global variables until later when that thread
	// can process them. Here, we process them.
	
	// Register any histograms waiting in the queue
	if( ! HISTOS_TO_REGISTER_CMSG.empty() ){
		REGISTRATION_MUTEX_CMSG.lock();
		for(auto h : HISTOS_TO_REGISTER_CMSG){
			if( RS_CMSG ) RS_CMSG->RegisterHistogram(h.second, (cMsgMessage*)h.first, true);
		}
		HISTOS_TO_REGISTER_CMSG.clear();
		REGISTRATION_MUTEX_CMSG.unlock();
	}
	
	// Register any histograms waiting in the queue
	if( ! HISTOS_TO_REGISTER_XMSG.empty() ){
		REGISTRATION_MUTEX_XMSG.lock();
		for(auto h : HISTOS_TO_REGISTER_XMSG){
			if( RS_XMSG ) RS_XMSG->RegisterHistogram(h);
		}
		HISTOS_TO_REGISTER_XMSG.clear();
		REGISTRATION_MUTEX_XMSG.unlock();
	}
}

//-------------------
// ExecuteMacro
//-------------------
void ExecuteMacro(TDirectory *f, string macro)
{
	// Lock ROOT
	pthread_rwlock_wrlock(ROOT_MUTEX);

	TDirectory *savedir = gDirectory;
	f->cd();
	
	// Keep a separate TSyle for each macro we draw. This used to
	// allow macros to change the style and have it stay changed
	// until the next macro is drawn.
	static std::map<string, TStyle*> styles;
	if( styles.count( macro ) == 0 ){
		styles[macro] = new TStyle();
	}
	*gStyle = *styles[macro];

	// Reset color palette to default. Macros may change this
	// with gStyle->SetPalette(...) but it is not actually a
	// property of the TSyle object. The SetPalette method just
	// maps to a static method of TColor which changes the palette
	// for the current TCanvas. (At least that is how I think this
	// works!)
	gStyle->SetPalette(); // reset to default 	

	// execute script line-by-line
	// maybe we should do some sort of sanity check first?
	istringstream macro_stream(macro);
	int iline = 0;
	while(!macro_stream.eof()) {
		string s;
		getline(macro_stream, s);
		iline++;

		Long_t err = gROOT->ProcessLine(s.c_str());
		if(err == TInterpreter::kProcessing){
			cout << "Processing macro ..." << endl;
		}else  if(err != TInterpreter::kNoError){
			cout << "Error processing the macro line " << iline << ": " << err << endl;
			cout << "\"" << s << "\"" << endl;
			break;
		}
	}

	// restore
	savedir->cd();
	
	// Unlock ROOT
	pthread_rwlock_unlock(ROOT_MUTEX);

}

//-----------
// ParseCommandLineArguments
// configuration priorities (low to high):
//    config file -> environmental variables -> command line
//-----------
void ParseCommandLineArguments(int &narg, char *argv[])
{
	if(VERBOSE>0) 
	_DBG_ << "In ParseCommandLineArguments().." << endl;
	
	
	// read from configuration file
	// TODO: decide how to set config filename on command line
	// the complication is that the config file settings should have the lowest priority
	string config_filename = getenv("HOME");  // + "/.RSTimeSeries";
	config_filename += "/.RSTimeSeries";
	const char *configptr = getenv("ROOTSPY_CONFIG_FILENAME");
	if(configptr) config_filename = configptr;  
	
//	INIReader config_reader(config_filename.c_str());
//	
//	if (config_reader.ParseError() < 0) {
//		std::cout << "Can't load configuration file '" << config_filename << "'" << endl;
//	} else {
//		
//		// [main]
//		ROOTSPY_UDL = config_reader.Get("main", "rootspy_udl",  "cMsg://127.0.0.1/cMsg/rootspy");
//		SESSION     = config_reader.Get("main", "session_name", "");
//		
//		POLL_DELAY      = config_reader.GetInteger("main", "poll_delay", 60);
//		MIN_POLL_DELAY  = config_reader.GetInteger("main", "min_poll_delay", 10);
//	}
	
	// allow for environmental variables
	const char *ptr = getenv("ROOTSPY_UDL");
	if(ptr)ROOTSPY_UDL = ptr;
	
	// check command line options
	static struct option long_options[] = {
		{"help",           no_argument,       0,  'h' },
		{"run-number",     required_argument, 0,  'R' },
		{"poll-delay",     required_argument, 0,  'p' },
		{"udl",            required_argument, 0,  'u' },
		{"server",         required_argument, 0,  's' },
		{"session-name",   no_argument,       0,  'S' },
		{"verbose",        no_argument,       0,  'V' },
		
		{0, 0, 0, 0  }
	};
	
	int opt = 0;
	int long_index = 0;
	while ((opt = getopt_long(narg, argv,"hR:p:u:s:A:BF:PHY:v", 
							  long_options, &long_index )) != -1) {
		switch (opt) {
			case 'R':
				if(optarg == NULL) Usage();
				RUN_NUMBER = atoi(optarg);
				break;
			case 'f' : 
				FORCE_START = true;
				break;
			case 'p' : 
				if(optarg == NULL) Usage();
				POLL_DELAY = atoi(optarg);
				break;
			case 'u' :
				if(optarg == NULL) Usage();
				ROOTSPY_UDL = optarg;
				break;
			case 's' :
				if(optarg == NULL) Usage();
				ROOTSPY_UDL = "cMsg://";
				ROOTSPY_UDL += optarg;
				ROOTSPY_UDL += "/cMsg/rootspy";
				break;
			case 'S' :
				SESSION = optarg;
				break;
			case 'v' :
				VERBOSE++;
				break;
				
			case 'h':
			default: Usage(); break;
		}
	}  
	
	// check any values
	if(POLL_DELAY < MIN_POLL_DELAY)
	POLL_DELAY = MIN_POLL_DELAY;
	
	// DUMP configuration
	cout << "-------------------------------------------------" << endl;
	cout << "Current configuration:" << endl;
	cout << "     ROOTSPY_UDL = " << ROOTSPY_UDL << endl;
	cout << "         SESSION = " << SESSION << endl;
	cout << "      RUN_NUMBER = " << RUN_NUMBER << endl;
	cout << "      POLL_DELAY = " << POLL_DELAY << endl;
	cout << "  MIN_POLL_DELAY = " << MIN_POLL_DELAY << endl;
	
	cout << "-------------------------------------------------" << endl;
	
}


//-----------
// Usage
//-----------
void Usage(void)
{
	cout<<"Usage:"<<endl;
	cout<<"       RSTimeSeries [options]"<<endl;
	cout<<endl;
	cout<<"Saves ROOT histograms being stored by programs running the RootSpy plugin."<<endl;
	cout<<endl;
	cout<<"Options:"<<endl;
	cout<<endl;
	cout<<"   -h,--help                 Print this message"<<endl;
	cout<<"   -u,--udl udl              UDL of cMsg RootSpy server"<<endl;
	cout<<"   -s,--server server-name   Set RootSpy UDL to point to server IP/hostname"<<endl;
	cout<<"   -R,--run-number number    The number of the current run" << endl;
	cout<<"   -p,--poll-delay time      Time (in seconds) to wait between polling seconds" << endl;
	cout<<"   -v,--verbose              Increase verbosity (can use multiple times)"<<endl;
	cout<<endl;
	cout<<endl;
	
	exit(0);
}

