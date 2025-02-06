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
#include <TFile.h>
#include <TDirectory.h>
#include <TObjArray.h>
#include <TH1F.h>
#include <TClass.h>
#include <TTree.h> 
#include <TList.h>

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <thread>
using namespace std;

#include "RSAggregator.h"

#include "DRootSpy.h"
#include "rs_cmsg.h"
#include "rs_info.h"

//////////////////////////////////////////////
// GLOBALS
//////////////////////////////////////////////
rs_xmsg *RS_XMSG = NULL;  // This is used by RootSpy libraries as external global
rs_xmsg* &RS_XMSG_SOURCES = RS_XMSG;  // This is used in this file to make naming more clear
DRootSpy *RS_XMSG_DESTINATIONS = NULL;
rs_cmsg *RS_CMSG = NULL;
rs_info *RS_INFO = NULL;
pthread_rwlock_t *ROOT_MUTEX = NULL;
string ROOTSPY_UDL;
string ROOTSPY_AGGREGATOR_UDL;

// These defined in rs_xmsg.cc
extern mutex REGISTRATION_MUTEX_XMSG;
extern set<rs_serialized*> HISTOS_TO_REGISTER_XMSG;
extern set<rs_serialized*> MACROS_TO_REGISTER_XMSG;

static int VERBOSE = 1;  // Verbose output to screen - default is to print out some information
bool DONE = false;


#if 0

bool MAKE_BACKUP = false;


// These defined in rs_cmsg.cc
extern mutex REGISTRATION_MUTEX_CMSG;
extern map<void*, string> HISTOS_TO_REGISTER_CMSG;
extern map<void*, string> MACROS_TO_REGISTER_CMSG;



// classes to generate optional output formats
HTMLOutputGenerator *html_generator = NULL;
PDFOutputGenerator *pdf_generator = NULL;


// These are used by the recursive CopyROOTDir to keep track of
// how many objects of each type are copied
int Ndirectories=0;
int Ntrees=0;
int Nhists=0;

// configuration variables
namespace config {
    static string ROOTSPY_UDL = "cMsg://127.0.0.1/cMsg/rootspy";
	//static string DAQ_UDL = "cMsg://127.0.0.1/cMsg";
	static string CMSG_NAME = "<not set here. see below>";
	static string SESSION = "";
	
	static TFile *CURRENT_OUTFILE = NULL;
    // name of file to keep the current state of monitoring histograms in
    // by default we also store the final state of the histograms here
	static string OUTPUT_FILENAME = "current_run.root";   
    // name of file to optionally store final histogram state in
	static string ARCHIVE_FILENAME = "<nofile>";         
	static string BACKUP_FILENAME = "backup.root"; 
	
	static double POLL_DELAY = 60;   // time between polling runs
	static double MIN_POLL_DELAY = 10;
	
	// run management 
	//bool KEEP_RUNNING = true;
	static bool FORCE_START = false;
	
	bool DONE = false;
	bool RUN_IN_PROGRESS = false;
	int RUN_NUMBER = 99999;   
	bool FINALIZE = false;
	
	static bool HTML_OUTPUT = false;
	static string HTML_BASE_DIR = "<nopath>";
	static bool PDF_OUTPUT = false;  
}

using namespace config;

#endif

//////////////////////////////////////////////
// FUNCTIONS
//////////////////////////////////////////////
void InitializeConnections(void);

void MainLoop(void);
void InitializeConnections(void);
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

//-----------
// signal_usr1_handler
//-----------
void signal_usr1_handler(int signum)
{
	cerr << "received signal " << signum << "..." << endl;

	vector<DRootSpy::hinfo_t> hinfos;
	gROOTSPY_RW_LOCK->acquire_read_lock();
	// pthread_rwlock_rdlock(gROOTSPY_RW_LOCK);
	if( gDirectory )
		if( RS_XMSG_DESTINATIONS ) RS_XMSG_DESTINATIONS->addRootObjectsToList(gDirectory, hinfos);

	uint32_t Nmacros_src = 0;
	for( auto hi : RS_INFO->hinfos ){
		if( hi.second.macroData != NULL ) Nmacros_src++;
	}

	map<string,macro_info_t> macros;
	if( RS_XMSG_DESTINATIONS ) RS_XMSG_DESTINATIONS->GetMacroInfos( macros );
	uint32_t Nmacros_dest = macros.size();

	gROOTSPY_RW_LOCK->release_lock();
	// pthread_rwlock_unlock(gROOTSPY_RW_LOCK);

	_DBG_ << "===== I Have " << hinfos.size() << " histograms!" << endl;
	_DBG_ << "===== I Have  " << Nmacros_dest << " macros from dest!" << endl;
	_DBG_ << "===== I Have   " << Nmacros_src << " macros from src!" << endl;
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
	if(signal(SIGUSR1, signal_usr1_handler)==SIG_ERR)
		cerr << "unable to set USR1 signal handler" << endl;


    // ------------------- initialization ---------------------------------
    InitializeConnections();

    // ------------------- periodic processing ----------------------------
    MainLoop();  // regularly poll servers for new histograms

    //-------------------------- cleanup ----------------------------------
    if( RS_XMSG_SOURCES      ) delete RS_XMSG_SOURCES;
    if( RS_XMSG_DESTINATIONS ) delete RS_XMSG_DESTINATIONS;

    return 0;
}


//-----------
// InitializeConnections
//-----------
void InitializeConnections(void)
{
    // Do this before anything else
    if(!ROOT_MUTEX){
    	ROOT_MUTEX = new pthread_rwlock_t;
    	pthread_rwlock_init(ROOT_MUTEX, NULL);
    }

    // Create rs_info object (this is used for the sources)
    RS_INFO = new rs_info();

    // ------------------- cMsg initialization ----------------------------
    // Create cMsg object
    char hostname[256];
    gethostname(hostname, 256);
    char str[512];
    sprintf(str, "RSAggregator_src_%d", getpid());
    auto XMSG_SRC_NAME = string(str);
	sprintf(str, "RSAggregator_dest_%d", getpid());
	auto XMSG_DEST_NAME = string(str);

	cout << "====================== Sources xMsg connection ===================" << endl;
	RS_XMSG_SOURCES      = new rs_xmsg(ROOTSPY_UDL, XMSG_SRC_NAME);
	cout << "=================================================================" << endl;
	cout << "=================== Destinations xMsg connection ================" << endl;
	RS_XMSG_DESTINATIONS = new DRootSpy(ROOT_MUTEX, ROOTSPY_AGGREGATOR_UDL, XMSG_DEST_NAME);
	RS_XMSG_DESTINATIONS->SetSumHistsOnly();
	cout << "=================================================================" << endl;

	if( RS_XMSG_SOURCES      ) RS_XMSG_SOURCES->verbose = VERBOSE;
	if( RS_XMSG_DESTINATIONS ) RS_XMSG_DESTINATIONS->VERBOSE = VERBOSE;
}

//-----------
// MainLoop
//-----------
void MainLoop(void)
{
	if(VERBOSE>3) _DBG_ << "Running main event loop..." << endl;

	// Make initial requests for histogram and macro lists
	RS_XMSG_SOURCES->RequestHists("rootspy");
	RS_XMSG_SOURCES->RequestMacroList("rootspy");

	// Loop until we are told to stop for some reason
	auto tlast = chrono::steady_clock::now() - chrono::seconds(4); // subtract 4 seconds so first iteration happens quicker
    while(!DONE) {

    	// Main loop iteratates up to 10 times per second
	    if(VERBOSE>7) _DBG_ << "------ MainLoop Iteration ------" << endl;

	    // Check for servers and histograms every few seconds
    	auto tnow = chrono::steady_clock::now();
    	auto tdiff = chrono::duration_cast<chrono::duration<double>>(tnow - tlast).count();
	    if(VERBOSE>7) _DBG_ << "  tdiff = " << tdiff << endl;
	    if( tdiff >= 5.0 ) {
		    if (RS_XMSG_SOURCES) {
			    if (VERBOSE > 1) _DBG_ << "  pinging servers ... " << endl;
			    RS_XMSG_SOURCES->PingServers();           // keeps the connections alive, and keeps the list of servers up-to-date
			    if (VERBOSE > 1) _DBG_ << "  requesting histograms ... " << endl;
			    RS_XMSG_SOURCES->RequestHists("rootspy"); // request all histograms
			    if (VERBOSE > 1) _DBG_ << "  requesting macros ... " << endl;
			    RS_XMSG_SOURCES->RequestMacroList("rootspy"); // request all macros

			    if (VERBOSE > 2) _DBG_ << "number of servers = " << RS_INFO->servers.size() << endl;

			    // Request all histograms from all servers
			    for (auto p : RS_INFO->servers) {
				    auto &s = p.second;
				    if (VERBOSE > 5) _DBG_ << "Requesting histograms from " << s.serverName << endl;
				    RS_XMSG_SOURCES->RequestHistograms(s.serverName, s.hnamepaths);
			    }

			    // Request all macros from all servers
			    for ( auto p : RS_INFO->histdefs ){
			    	auto &hnamepath = p.first;
			    	auto &hdef = p.second;
			    	if( hdef.type == hdef_t::macro ) {
			    		for( auto pp : hdef.servers ){
			    			auto server = pp.first;
			    			RS_XMSG_SOURCES->RequestMacro( server, hnamepath );
			    		}
			    	}

			    }

			    // Copy all macros received from sources into our DRootSpy object
			    for( auto hi : RS_INFO->hinfos ){
				    if( !hi.second.macroString.empty() ){
				    	auto &hnamepath = hi.second.hnamepath;
				    	auto pos = hnamepath.find_last_of("/");
				    	auto name = hnamepath.substr(pos+1);
				    	auto path = hnamepath.substr(0,pos);
				    	auto &macroString = hi.second.macroString;
//				    	_DBG_<<"++++++++++++ hnamepath=" << hnamepath << " path=" << path << " name="<<name << endl;
					    if( RS_XMSG_DESTINATIONS ) RS_XMSG_DESTINATIONS->RegisterMacro(name, path, macroString);
				    }
			    }


		    }

		    tlast = chrono::steady_clock::now();
	    }

	    // The RootSpy GUI requires all macros and histograms be
	    // processed in the main ROOT GUI thread to avoid crashes.
	    // Thus, when messages with histgrams come in they are
	    // stored in global variables until later when that thread
	    // can process them. Here, we process them.

	    // Register any histograms waiting in the queue
	    if( ! HISTOS_TO_REGISTER_XMSG.empty() ){
		    if(VERBOSE>2) _DBG_<< "HISTOS_TO_REGISTER_XMSG.size()=" << HISTOS_TO_REGISTER_XMSG.size() << endl;
	    	lock_guard<mutex> lck(REGISTRATION_MUTEX_XMSG);
		    if( RS_XMSG ) {
			    for (auto h : HISTOS_TO_REGISTER_XMSG) {
				    RS_XMSG->RegisterHistogram(h);
				    delete h;
			    }
		    }
		    HISTOS_TO_REGISTER_XMSG.clear();
	    }

	    // Register any macros waiting in the queue
	    if( ! MACROS_TO_REGISTER_XMSG.empty() ){
		    if(VERBOSE>2) _DBG_<< "MACROS_TO_REGISTER_XMSG.size()=" << MACROS_TO_REGISTER_XMSG.size() << endl;
		    lock_guard<mutex> lck(REGISTRATION_MUTEX_XMSG);
		    if( RS_XMSG ) {
			    for (auto h : MACROS_TO_REGISTER_XMSG) {
				    RS_XMSG->RegisterMacro(h);
				    delete h;
			    }
		    }
		    MACROS_TO_REGISTER_XMSG.clear();
	    }

	    // Send requests to producers for any histograms we were requested to provide
	    set<string> hnamepaths;
	    if( RS_XMSG_DESTINATIONS ) RS_XMSG_DESTINATIONS->PopRequested_hnamepaths( hnamepaths );
	    if( !hnamepaths.empty() ) {
	    	_DBG_<<"+++++++++++ Passing requests for " << hnamepaths.size() << " histograms ... " << endl;
		    vector<string> vhnamepaths( hnamepaths.begin(), hnamepaths.end() );
	    	RS_XMSG_SOURCES->RequestHistograms( "rootspy", vhnamepaths );
	    }

	    // Sleep a small amount of time
        std::this_thread::sleep_for( std::chrono::milliseconds(100));
    }

	if(VERBOSE>3) _DBG_ << "Finished main event loop." << endl;

}

//-----------
// ParseCommandLineArguments
// configuration priorities (low to high):
//    config file -> environmental variables -> command line
//-----------
void ParseCommandLineArguments(int &narg, char *argv[])
{
    if(VERBOSE>3)   _DBG_ << "In ParseCommandLineArguments().." << endl;

    
    // allow for environmental variables
    const char *ptr = getenv("ROOTSPY_UDL");
    if(ptr)ROOTSPY_UDL = ptr;
    
    ptr = getenv("ROOTSPY_AGGREGATOR_UDL");
    if(ptr)ROOTSPY_AGGREGATOR_UDL = ptr;

    for( int i=1; i<narg; i++) {
		string arg = argv[i];
		string next = (i<(narg-1)) ? argv[i+1]:"";

		if( arg=="-h" || arg=="--help") {
			Usage();
		}else if( arg=="-p" || arg=="--pudl" ) {
			ROOTSPY_UDL = next;
			i++;
		}else if( arg=="-c" || arg=="--cudl" ) {
			ROOTSPY_AGGREGATOR_UDL = next;
			i++;
		}else if( arg=="-v" || arg=="--verbose" ) {
			VERBOSE++;
		}else {
			cerr << "\n\nUnknown argument: " << arg << "\n\n" <<endl;
			exit(-1);
		}
    }

    if( ROOTSPY_UDL.length()==0 ){
    	cerr << "\n\n\nERROR: ROOTSPY_UDL *must* be specified either via environment or --pudl option!\n\n\n" << endl;
    	exit(-1);
    }
	if( ROOTSPY_AGGREGATOR_UDL.length()==0 ){
		cerr << "\n\n\nERROR: ROOTSPY_AGGREGATOR_UDL *must* be specified either via environment or --cudl option!\n\n\n" << endl;
		exit(-1);
	}

    // DUMP configuration
    cout << "-------------------------------------------------" << endl;
    cout << "Current configuration:" << endl;
    cout << "           ROOTSPY_UDL: " << ROOTSPY_UDL << endl;
	cout << "ROOTSPY_AGGREGATOR_UDL: " << ROOTSPY_AGGREGATOR_UDL << endl;
	cout << "               VERBOSE: " << VERBOSE << endl;
    cout << "-------------------------------------------------" << endl;
}

//-----------
// Usage
//-----------
void Usage(void)
{
    cout<<"Usage:"<<endl;
    cout<<"       RSAggregator [options]"<<endl;
    cout<<endl;
    cout<<"Aggregates histograms published on one UDL and re-publishes summed histos only to another UDL."<<endl;
    cout<<endl;
    cout<<"Options:"<<endl;
    cout<<endl;
    cout<<"   -h,--help                 Print this message"<<endl;
    cout<<"   -p,--pudl udl             xMsg UDL for histogram producers"<<endl;
	cout<<"   -c,--cudl udl             xMsg UDL for histogram consumers"<<endl;
    cout<<"   -v,--verbose              Increase verbosity (can use multiple times)"<<endl;
    cout<<endl;
    cout<<endl;
    
    exit(0);
}

