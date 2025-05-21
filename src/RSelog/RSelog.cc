// RSArchiver
// Program to archive data over RootSpy into ROOT files
// The current design of this program is to be 

#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#include <TROOT.h>
#include <TCanvas.h>
#include <TFile.h>
#include <TDirectory.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TClass.h>
#include <TTree.h> 
#include <TStyle.h>
#include <TLatex.h>

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <mutex>
using namespace std;


#include "rs_cmsg.h"
#include "rs_info.h"

//////////////////////////////////////////////
// GLOBALS
//////////////////////////////////////////////
rs_cmsg *RS_CMSG = NULL;
rs_xmsg *RS_XMSG = NULL;
rs_info *RS_INFO = NULL;
pthread_rwlock_t *ROOT_MUTEX = NULL;

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
static string ROOTSPY_UDL = "cMsg://127.0.0.1/cMsg/rootspy";
static string CMSG_NAME = "<not set here. see below>";
static set<string> HNAMEPATHS;
static string TMPDIR = "/home/hdops/tmp";
static string ELOG_NAME = "TLOG";
static bool ELOG_NOTIFY = false;
static string ELOG_EMAIL = "";
static uint32_t TIMEOUT_SECS=25; // maximum seconds to wait for histograms or macros before giving up

bool DONE = false;
int RUN_NUMBER = 0;   


//////////////////////////////////////////////
// FUNCTIONS
//////////////////////////////////////////////

void signal_stop_handler(int signum);

bool GetHists(const set<string> &hnamepaths, bool send_request=true);
void RegisterQueuedItems(void);
void ExecuteMacro(TDirectory *f, string macro);
void ResetCanvas(TCanvas* c);
void ParseCommandLineArguments(int &narg, char *argv[]);
void Usage(void);


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
	if(signal(SIGHUP, signal_stop_handler)==SIG_ERR) cerr << "unable to set HUP signal handler" << endl;
	if(signal(SIGINT, signal_stop_handler)==SIG_ERR) cerr << "unable to set INT signal handler" << endl;

	// Create a lock for controlling access to ROOT Global
	ROOT_MUTEX = new pthread_rwlock_t;
	pthread_rwlock_init(ROOT_MUTEX, NULL);

	// Create rs_info object
	RS_INFO = new rs_info();

	// ------------------- cMsg initialization ----------------------------
	// Create cMsg object
	char hostname[256];
	gethostname(hostname, 256);
	char str[512];
	sprintf(str, "RSelog_%d", getpid());
	CMSG_NAME = string(str);
	cout << "RootSpy UDL is " << ROOTSPY_UDL << endl;
	if( USE_CMSG ){
		RS_CMSG = new rs_cmsg(ROOTSPY_UDL, CMSG_NAME);
		RS_CMSG->verbose = VERBOSE;
	}
	if( USE_XMSG ) RS_XMSG = new rs_xmsg(ROOTSPY_UDL, CMSG_NAME);


	// Wait for RS_CMSG to launch cMsg threads and ping servers
	sleep(2);
	
	// Request all histogram definitions first so RootSpy will know what to do with them
	// when the histograms come.
	cout << endl << "------------------------------------------------------" << endl;
	cout << "Requesting all histogram and macro definitions ... " << endl;
	if( RS_CMSG ) RS_CMSG->RequestHists("rootspy");
	if( RS_CMSG ) RS_CMSG->RequestMacroList("rootspy");
	if( RS_XMSG ) RS_XMSG->RequestHists("rootspy");
	if( RS_XMSG ) RS_XMSG->RequestMacroList("rootspy");
	usleep(500000);
	
	// Initial request of all hnamepaths. Most likely these are macros.
	// When the macro arrives however, rootspy immediately sends out a
	// request for any histograms it needs.
	bool good = GetHists(HNAMEPATHS);
	if( !good ){
		cerr << "Unable to get all hnamepaths in initial set!" << endl;
		if(RS_CMSG) delete RS_CMSG;
		if(RS_XMSG) delete RS_XMSG;
		if(RS_INFO) delete RS_INFO;
		exit(-1);
	}

	// Create TCanvas, but set ROOT to batch mode so that it doesn't actually open a window.
	gROOT->SetBatch(kTRUE);
	TCanvas *c1 = new TCanvas("c1", "A Canvas", 1600, 1200);

	// Setup interpretor so macros don't have to include these things.
	gROOT->ProcessLine("#include <iostream>");
	gROOT->ProcessLine("using namespace std;");
	
	// Loop over hnamepaths, drawing each into the canvas and saving the image
	int ipage=1;
	vector<string> img_files;
	map<string, string> hnamepath_by_img_file;
	stringstream ss_log; // record messages about why an hnamepath is unavailable for adding to log entry
	for(string hnamepath : HNAMEPATHS){
	
		cout << endl << "--- Processing " << hnamepath << " ---" << endl;

	
		// Get hdef info for this hnamepath
		set<string> macro_hnamepaths;
		string macroString;
		RS_INFO->Lock();
		const hdef_t *hdef = NULL;
		for(auto hdefpair : RS_INFO->histdefs){
			if( hnamepath == hdefpair.second.hnamepath ) {
				hdef = &hdefpair.second;
				macro_hnamepaths = hdef->macro_hnamepaths;
				if(!hdef->hists.empty()) macroString = hdef->hists.begin()->second.macroString; // Use first hinfo
				break;
			}
		}
		RS_INFO->Unlock();
		if(!hdef) {
			cerr << "Hmmm ... could not find hdef for " << hnamepath << " but it should be here!?!? -- skipping" << endl;
			ss_log << "No hdef for \"" << hnamepath << "\"  -- skipped" << endl;
			continue;
		}
		
		// Get any additional hnamepaths this may require (if it is a macro)
		bool skip = false;
		if( ! macro_hnamepaths.empty() ){
			bool good = GetHists(macro_hnamepaths,true);
			if( !good ){
				cerr << "Could not find all hnamepaths needed for macro " << hnamepath << " -- skipping " << endl;
				ss_log << "Could not find all " << macro_hnamepaths.size() << " hnamepaths needed for macro \"" << hnamepath << "\"  -- skipped" << endl;
				skip = true;
			}
		}
		if(skip) continue;
		
		// Clear canvas
		c1->Clear();

		// Check if this is a macro or regular histogram
		if(macroString != ""){
			cout << "Executing macro for " << hnamepath << endl;
			ExecuteMacro(RS_INFO->sum_dir, macroString);
		}else{
			// This is not entirely thread safe!
			if(hdef->sum_hist) {
				if( hdef->sum_hist->InheritsFrom("TH2") ){
					hdef->sum_hist->Draw("colz");
				}else{
					hdef->sum_hist->Draw();
				}
			}
		}
		
		// Update canvas and save to PNG file
		c1->Update();
		cout << "    - Writing plot " << ipage << " of " << HNAMEPATHS.size() << endl;
		char fname[256];
		sprintf(fname,"%s/rs_page%02d.png", TMPDIR.c_str(), ipage);
		c1->SaveAs(fname, "");
		img_files.push_back(fname);
		hnamepath_by_img_file[fname] = hnamepath;
		ipage++;

		RS_INFO->Unlock();
	}
	
	// ---------------- Create e-log page and submit ----------------------
		
	char fname[256];
	sprintf(fname, "%s/elog_monitoring.html", TMPDIR.c_str());
	ofstream ofs(fname);
	if(ofs.is_open()){
		time_t t = time(NULL);
		ofs << "Hall-D Monitoring Plots for " << ctime(&t);
		ofs << "<p>" << img_files.size() << "/" << HNAMEPATHS.size() << " pages generated </p>" << endl;
		ofs << "<pre>" << ss_log.str() << "</pre>" << endl;
		ofs << "<hr>" << endl;
		ofs.close();
		
		stringstream cmd;
		cmd << "/site/ace/certified/apps/bin/logentry";
		cmd << " --html -b " << fname;
		cmd << " -l " << ELOG_NAME;
		if(ELOG_NOTIFY) cmd << " -n " << ELOG_EMAIL;

		if(RUN_NUMBER>0){
			cmd << " -t \"Hall-D Monitoring Plots Run " << RUN_NUMBER << "\"";
		}else{
			cmd << " -t \"Hall-D Monitoring Plots\"";
		}

		// attach all plots
		for( string s : img_files ) cmd << " -a " << s << " -c \"" << hnamepath_by_img_file[s] << "\"";
		
		// Save command in tmp directory for debugging
		ofstream ofcmd((TMPDIR + "/occ_elog_cmd").c_str());
		ofcmd << cmd.str() << endl;
		ofcmd.close();
		
		cout << "Executing:" << endl;
		cout << "   " << cmd.str() << endl;
		
		system(cmd.str().c_str());
	}
	
	cout << endl;
	cout << "Finished making e-log entry" << endl;
	cout << "--------------------------------" << endl;
	

	// Clean up
	if( RS_CMSG )delete RS_CMSG;
	if( RS_XMSG )delete RS_XMSG;
	delete RS_INFO;

	cout << "Done" << endl;

	return 0;
}

//-----------
// GetHists
//-----------
bool GetHists(const set<string> &hnamepaths, bool send_request)
{
	// Send requests for all of the listed hnamepaths and
	// then wait until at least one server responds with each.
	// A check is first made that all histogram definitons are
	// already present and this will wait up to TIMEOUT_SECS 
	// for them to come in. (They are all requested at the beginning
	// of the program.)
	// 
	// Once all definitions are present, requests for the hnamepaths
	// themselves are sent. A minimum of 2 seconds is spent waiting
	// in order to  give multiple servers a chance to respond. A maximum
	// of TIMEOUT_SECS is spent waiting. This may also spend less than
	// 2 seconds waiting if the received times of all relevant histograms
	// indicate a request was sent out at least 2 seconds earlier.
	// If all hists are found then true is returned. Otherwise false is
	// returned.

	// ------------- Wait for histogram definitions --------------
	cout << "Checking for histogram definitions ..." << endl;
	uint32_t Ntries = 0;
	while(!DONE){

		// Check received hdefs
		set<string> found_hnamepaths;
		RS_INFO->Lock();
		for(auto hdef : RS_INFO->histdefs){
			for(string s : hnamepaths) if( s == hdef.second.hnamepath ) found_hnamepaths.insert(s);
		}
		RS_INFO->Unlock();

		// Update ticker and finish if appropriate
		cout << "  Received " << found_hnamepaths.size() << " / " << hnamepaths.size() << " definitions  \r";
		cout .flush();

		if(found_hnamepaths.size() == hnamepaths.size()) break;

		uint32_t sleep_tics = 200000;
		uint32_t max_tries = TIMEOUT_SECS*(1000000/sleep_tics); // 2 seconds

		if(Ntries >= max_tries) {
			cerr << endl << "Timed out waiting for hnamepath definitions!" << endl;
			return false;
		}

		usleep(sleep_tics);
		Ntries++;
	}

	// ------------- Request all hnamepaths --------------
	if(send_request){
		cout << "Requesting hnamepaths:" << endl;
		for(string s : hnamepaths){
			cout << "   " << s << endl;
			if( RS_CMSG )RS_CMSG->RequestHistogram("rootspy", s);
			if( RS_CMSG )RS_CMSG->RequestMacro("rootspy", s);
			if( RS_XMSG )RS_XMSG->RequestHistogram("rootspy", s);
			if( RS_XMSG )RS_XMSG->RequestMacro("rootspy", s);
		}
	}

	// ------------- Wait for histograms --------------
	cout << endl << "Waiting for histograms .... " << endl;
	Ntries = 0;
	while(!DONE){
	
		// current time in seconds 
		double now = 0.0;
		if( RS_CMSG ) now = RS_CMSG->GetTime();
		if( RS_XMSG ) now = RS_XMSG->GetTime();

		// Register any ROOT items or macros queued up by rs_cmsg
		RegisterQueuedItems();

		// Check received hists
		set<string> found_hnamepaths;
		uint32_t Nhists = 0; // total histograms recieved
		double youngest=0.0; // the hdef whose oldest received histogram is yougest 
		RS_INFO->Lock();
		for(auto hdef : RS_INFO->histdefs){
			for(string s : hnamepaths){
				if( s == hdef.second.hnamepath ){
					if( !hdef.second.hists.empty() ){
						found_hnamepaths.insert(s);
						Nhists += hdef.second.hists.size();
						double oldest = now;
						for(auto hinfopair : hdef.second.hists){
							double received = hinfopair.second.received;
							if(received < oldest) oldest = received;
						}
						
						// This keeps track of the hdef whose oldest received
						// histogram is the youngest of all hdefs.						
						if( oldest > youngest ) youngest = oldest;
					}
				}
			}
		}
		RS_INFO->Unlock();

		// Update ticker
		cout << "  Received " << found_hnamepaths.size() << " / " << hnamepaths.size() << "  (" << Nhists << " hists)  \r";
		cout .flush();

		uint32_t sleep_tics = 200000;
		uint32_t min_tries = (2*1000000)/sleep_tics; // 2 seconds
		uint32_t max_tries = TIMEOUT_SECS*(1000000/sleep_tics);
				
		// Check if everything is found
		if(found_hnamepaths.size() == hnamepaths.size()) {
			if(Ntries >= min_tries)break;
			
			// This allows us to break earlier than the minimum time if we see that
			// all hnamepaths have been found and all have at least one received
			// histogram that is the minimum age old. The idea is that we must have
			// already requested that hnamepath that long ago so there is no need to
			// wait an additional min age.
			if( (now-youngest) >=  (double)min_tries*(double)sleep_tics/1.0E6) break;
		}

		// Check if past timeout
		if(Ntries >= max_tries) {
			cerr << endl << "Timed out waiting for hists!" << endl;
			for(auto hnamepath: hnamepaths){
				cout << hnamepath << ": " << (found_hnamepaths.count(hnamepath) ? "found":"not found") << endl;
			}
			return false;
		}

		// sleep for a cycle
		Ntries++;
		usleep(sleep_tics);
	}
	
	return true;
}

//-----------
// RegisterQueuedItems
//-----------
void RegisterQueuedItems(void)
{
	// This was copied from rs_mainframe::DoTimer in the RootSpy GUI program.
	// That tries to funnel all ROOT operations through the main GUI thread
	// so in order to actually create ROOT objects, we must process the queues
	// here.

	// Register any histograms waiting in the queue
	if( ! HISTOS_TO_REGISTER_CMSG.empty() ){
		REGISTRATION_MUTEX_CMSG.lock();
		for(auto h : HISTOS_TO_REGISTER_CMSG){
			if( RS_CMSG ) RS_CMSG->RegisterHistogram(h.second, (cMsgMessage*)h.first, true);
		}
		HISTOS_TO_REGISTER_CMSG.clear();
		REGISTRATION_MUTEX_CMSG.unlock();
	}
	
	// Register any macros waiting in the queue
	if( ! MACROS_TO_REGISTER_CMSG.empty() ){
		REGISTRATION_MUTEX_CMSG.lock();
		for(auto m : MACROS_TO_REGISTER_CMSG){
			if( RS_CMSG ) RS_CMSG->RegisterMacro(m.second, (cMsgMessage*)m.first);
		}
		MACROS_TO_REGISTER_CMSG.clear();
		REGISTRATION_MUTEX_CMSG.unlock();
	}

	// Register any histograms waiting in the queue
	if( ! HISTOS_TO_REGISTER_XMSG.empty() ){
		REGISTRATION_MUTEX_XMSG.lock();
		for(auto h : HISTOS_TO_REGISTER_XMSG){
			if( RS_XMSG ) RS_XMSG->RegisterHistogram(h);
			delete h;
		}
		HISTOS_TO_REGISTER_XMSG.clear();
		REGISTRATION_MUTEX_XMSG.unlock();
	}
	
	// Register any macros waiting in the queue
	if( ! MACROS_TO_REGISTER_XMSG.empty() ){
		REGISTRATION_MUTEX_XMSG.lock();
		for(auto m : MACROS_TO_REGISTER_XMSG){
			if( RS_XMSG ) RS_XMSG->RegisterMacro(m);
			delete m;
		}
		MACROS_TO_REGISTER_XMSG.clear();
		REGISTRATION_MUTEX_XMSG.unlock();
	}
}

//-------------------
// ExecuteMacro
//-------------------
void ExecuteMacro(TDirectory *f, string macro)
{
	// Copied from rs_mainframe.cc and modified for here.

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

	// Reset all attributes of TCanvas to ensure one macro does not 
	// bleed settings to another.
	if (gPad && gPad->InheritsFrom("TCanvas")) {
		ResetCanvas(static_cast<TCanvas*>(gPad));
	}

	TVirtualPad *top_pad = gPad;

	// execute script line-by-line
	// maybe we should do some sort of sanity check first?
	istringstream macro_stream(macro);
	int iline = 0;
	while(!macro_stream.eof()) {
		string s;
		getline(macro_stream, s);
		iline++;
		
		// This macro should have been parsed upon receipt and all histograms
		// already requested and received based on special comments. Thus,
		// we do not need to request them here.
		
		// Process line
		Long_t err = gROOT->ProcessLine(s.c_str());
		if(err == TInterpreter::kProcessing){
			cout << "Processing macro ..." << endl;
		}else  if(err != TInterpreter::kNoError){
			cout << "Error processing the macro line " << iline << ": " << err << endl;
			cout << "\"" << s << "\"" << endl;
			break;
		}
	}

	// Optionally add a run number label
	if( RUN_NUMBER > 0 ){
		TLatex *run_number_label = new TLatex();
		run_number_label->SetTextSize(1.0);
		run_number_label->SetTextColor(kBlack);
		run_number_label->SetTextAlign(22);

		top_pad->cd();
		TPad *pad = (TPad*)gDirectory->FindObjectAny("run_number_label_pad");
		if(!pad) pad = new TPad("run_number_label_pad", "Run Number", 0.91, 0.975, 1.0, 1.0);
		pad->Draw();
		pad->cd();
		char str[256];
		sprintf(str, "Run: %d", (int)RUN_NUMBER);
		run_number_label->DrawLatex(0.5, 0.5, str);
		pad->Update();
	}

	// restore
	savedir->cd();

	// Unlock ROOT
	pthread_rwlock_unlock(ROOT_MUTEX);

}

//-------------------
// ResetCanvas
//
// Reset the attributes of the existing canvas to defaults. This is called
// before executing a macro to ensure there are no lingering settings from
// the previous macro that may have drawn on this canvas.
//
// Upon entry, it is assumed that the gStyle object has been updated already
// so that settings from it may be reapplied.
//-------------------
void ResetCanvas(TCanvas* c) {

	if( c == nullptr ) return; // bulletproof

	// clear primatives (this will already have been done, but this does not hurt)
	c->Clear();
  
	// reset pad attributes
	c->ResetAttPad();
	c->ResetAttLine();
	c->ResetAttFill();
  
	// reapply global style
	c->UseCurrentStyle();
  
	// disable any log scales
	c->SetLogx(0);
	c->SetLogy(0);
	c->SetLogz(0);
  
	// finally, update the display
	c->Update();
}

//-----------
// ParseCommandLineArguments
//-----------
void ParseCommandLineArguments(int &narg, char *argv[])
{
	if(VERBOSE>1) _DBG_ << "In ParseCommandLineArguments().." << endl;
		
	// allow for environment variables
	const char *ptr = getenv("ROOTSPY_UDL");
	if(ptr)ROOTSPY_UDL = ptr;

	// check command line options
	static struct option long_options[] = {
		{"help",           no_argument,       0,  'h' },
		{"run-number",     required_argument, 0,  'R' },
		{"logbook",        required_argument, 0,  'L' },
		{"udl",            required_argument, 0,  'u' },
		{"server",         required_argument, 0,  's' },
		{"timeout",        required_argument, 0,  't' },
		{"email",          required_argument, 0,  'e' },
		{"hnamepath",      required_argument, 0,  'H' },
		{"verbose",        no_argument,       0,  'V' },
		{0, 0, 0, 0  }
	};
    
	int opt = 0;
	int long_index = 0;
	while ((opt = getopt_long(narg, argv,"hR:L:u:s:e:H:v", long_options, &long_index )) != -1) {
		switch (opt) {
			case 'R':
				if(optarg == NULL) Usage();
				RUN_NUMBER = atoi(optarg);
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
			case 't' :
				TIMEOUT_SECS = atoi(optarg);;
				break;
			case 'L' :
				if(optarg == NULL) Usage();
				ELOG_NAME = optarg;
				break;
			case 'e' :
				if(optarg == NULL) Usage();
				ELOG_EMAIL = optarg;
				ELOG_NOTIFY = true;
				break;
			case 'v' :
				VERBOSE++;
				break;
			case 'h':
				Usage();
				break;
			case 'H':
				HNAMEPATHS.insert(optarg);
				break;
		}
	}
	
	if(HNAMEPATHS.empty()){
		cout << endl << "You must specify at leat one hnamepath!" << endl << endl;
		Usage();
	}

	// make sure tmp directory exists
	struct stat sb;
    if( !(stat(TMPDIR.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) ) TMPDIR = ".";

    // DUMP configuration
    cout << "-------------------------------------------------" << endl;
    cout << "Current configuration:" << endl;
    cout << "       ROOTSPY_UDL = " << ROOTSPY_UDL << endl;
    cout << "        RUN_NUMBER = " << RUN_NUMBER << endl;
	cout << "             E-LOG = " << ELOG_NAME << endl;
	cout << "     TMP Directory = " << TMPDIR << endl;
	cout << "   Number of plots = " << HNAMEPATHS.size() << endl;
	if(ELOG_NOTIFY) cout << "Notification email = " << ELOG_EMAIL << endl;
    cout << "-------------------------------------------------" << endl;

}

//-----------
// Usage
//-----------
void Usage(void)
{
	cout<<"Usage:"<<endl;
	cout<<"       RSArchiver [options]"<<endl;
	cout<<endl;
	cout<<"Saves ROOT histograms being stored by programs running the RootSpy plugin."<<endl;
	cout<<endl;
	cout<<"Options:"<<endl;
	cout<<endl;
	cout<<"   -h,--help                 Print this message"<<endl;
	cout<<"   -u,--udl udl              UDL of cMsg RootSpy server"<<endl;
	cout<<"   -L,--logbook logbook      Logbook to make entry (def." << ELOG_NAME << ")" <<endl;
	cout<<"   -s,--server server-name   Set RootSpy UDL to point to server IP/hostname"<<endl;
	cout<<"   -t,--timeout seconds      Set maximum time to wait for histograms or macros (total may be double this)"<<endl;
	cout<<"   -R,--run-number number    The number of the current run" << endl;
	cout<<"   -H,--hnamepath hnamepath  An hnamepath to include (can be multiple of these)" << endl;
	cout<<"   -e,--email                Comma separated list of email addresses to notify" << endl;
	cout<<"   -v,--verbose              Increase verbosity (can use multiple times)"<<endl;
	cout<<endl;
	cout<<endl;

	exit(0);
}

