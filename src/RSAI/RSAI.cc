// RSTimeSeries
//
// Program to periodically run macros to create images that can be used
// to infer an AI to tell us whether the data looks OK. This program
// will create the image files, but defers to the external "hydra"
// program to actually apply it to the model and then take action based
// on the result.
//

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
#include <TStyle.h>
#include <TInterpreter.h>
#include <TLatex.h>
#include <TPad.h>
#include <TASImage.h>
#include <TCanvas.h>

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <dirent.h>

using namespace std;

#include "RSAI.h"

#include "rs_cmsg.h"
#include "rs_info.h"
#include "rs_macroutils.h"

//////////////////////////////////////////////
// GLOBALS
//////////////////////////////////////////////
TCanvas *c1 = nullptr;
static int VERBOSE = 1;  // Verbose output to screen - default is to print out some information
map<string, map<int, int> > CHUNK_COUNTER; // key=base_name(i.e. of macro/file) val=map of pads with key=pad number and val=counter


// The following is passed to the macros as a means to set the scale for how often
// images should be produced. Most occupancy plots will just use this directly as
// the number of events processed while making the plot.
int MIN_EVENTS_RSAI = 1.0E5;


rs_cmsg *RS_CMSG = NULL;
rs_xmsg *RS_XMSG = NULL;
rs_info *RS_INFO = NULL;
pthread_rwlock_t *ROOT_MUTEX = NULL;
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


// configuration variables
namespace config {
	static string ROOTSPY_UDL = "cMsg://127.0.0.1/cMsg/rootspy";
	static string CMSG_NAME = "<not set here. see below>";
	// static string SESSION = "";
		
	static double POLL_DELAY = 10;   // time between polling runs
	static double MIN_POLL_DELAY = 5;
	
	// run management 
	//bool KEEP_RUNNING = true;
	// static bool FORCE_START = false;
	
	bool DONE = false;
	bool RUN_IN_PROGRESS = false;
	int RUN_NUMBER = 99999;
	string OUTPUT_DIR = ".";
	string SYMLINK_DIR = "";
	
}

using namespace config;


//////////////////////////////////////////////
// FUNCTIONS
//////////////////////////////////////////////

void BeginRun();
void MainLoop(void);
void GetAllHists(uint32_t Twait=2); // Twait is in seconds
void ExecuteMacro(TDirectory *f, string macro);
void ResetCanvas(TCanvas* c);
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
	snprintf(str, 512,  "RSAI_%d", getpid());
	CMSG_NAME = string(str);
	cout << "Full UDL is " << ROOTSPY_UDL << endl;
	if( USE_CMSG ) RS_CMSG = new rs_cmsg(ROOTSPY_UDL, CMSG_NAME);
	if( USE_XMSG ) RS_XMSG = new rs_xmsg(ROOTSPY_UDL, CMSG_NAME);
	
	// set session name to some default
	// if(SESSION.empty()) SESSION="halldsession";

	// Create TCanvas, but set ROOT to batch mode so that it doesn't actually open a window.
	gROOT->SetBatch(kTRUE);
	c1 = new TCanvas("c1", "A Canvas", 1600, 1200);

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
	gROOT->ProcessLine("extern \"C\" { void rs_SavePad(const string fname, int ipad); }");
	gROOT->ProcessLine("extern \"C\" { void InsertSeriesData(string sdata){} }"); // disable insertion of series data for RootSpy GUI
	gROOT->ProcessLine("extern \"C\" { void InsertSeriesMassFit(string ptype, double mass, double width, double mass_err, double width_err, double unix_time=0.0){} }"); // (same as previous)

	// Set flag so macros will NOT automatically reset histograms after
	// a successful fit. This flag is used by RSTimeSeries
	rs_SetFlag("RESET_AFTER_FIT", 0);

	// Set flag so macros will automatically reset histograms after
	// a successful fit. This flag is used by RSAI
	rs_SetFlag("Is_RSAI", 1);
	rs_SetFlag("RESET_AFTER_SAVEPAD", 1);
	rs_SetFlag("MIN_EVENTS_RSAI", MIN_EVENTS_RSAI);

	// The following ensures that the routines in rs_macroutils are
	// linked in to the final executable so they are available to 
	// the macros.
	if(rs_GetFlag("RESET_AFTER_FIT") != 0){
		// (should never actually get here)
		rs_ResetHisto("N/A");
		rs_RestoreHisto("N/A");
		rs_SavePad("N/A", 0);
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
		std::set<string> fnames_written;
		RS_INFO->Lock();
		for(auto &hnamepath : MACRO_HNAMEPATHS){
			auto hdef = RS_INFO->histdefs[hnamepath];
			if(hdef.hists.empty()) continue;
			auto the_hinfo = hdef.hists.begin()->second;
			c1->cd();
			c1->Clear();
			if(VERBOSE>1)  _DBG_ << "Executing macro: " << hnamepath << endl;
			if(the_hinfo.Nkeys == 1){
				ExecuteMacro(RS_INFO->sum_dir, the_hinfo.macroString);
			}else{
				ExecuteMacro(the_hinfo.macroData, the_hinfo.macroString);
			}

			// Due to some long standing bug in TPad, the SaveAs method does not work
			// properly and we are advised to use only TCanvas::SaveAs. See:
			// https://sft.its.cern.ch/jira/browse/ROOT-7633
			//
			// Thus, if we are to save any pads, then we must save the entire canvas
			// first and then crop the pads from that. Technically, there should be a way
			// to do this in memory by getting the TASImage of the canvas directly, but
			// that looks to be rather complicated.
			if( ! rs_PadsToSave.empty() ){

				// Save whole canvas to temporary file.
				string tmp_img_fname = OUTPUT_DIR + "/tmp_canvas.png";
				c1->Update();
				c1->SaveAs(tmp_img_fname.c_str());

				// Loop over base filenames. Generally, there will only be one of these.
				for( auto p : rs_PadsToSave ) {
					auto &basename = p.first;

					// Loops over pads to save using this base filename
					for (int ipad : p.second) {

						// If the pad has a non-default name, use that. Otherwise, use a zero-padded pad number
						auto *pad = c1->GetPad(ipad);
						stringstream default_pad_name;
						default_pad_name << c1->GetName();
						if( ipad != 0 ) default_pad_name << "_" << ipad;
						string pad_name = pad->GetName();

						// Standard filename format includes pad name or number and time "chunk"
						char fname_base[256];
						if( ipad == 0 ){
							// Whole canvas excludes pad from fname
							snprintf(fname_base, 256, "%s_%04d.png", basename.c_str(), ++CHUNK_COUNTER[basename][ipad]);
						}else if( pad_name == default_pad_name.str() ){
							// Use pad number in fname
							snprintf(fname_base, 256, "%s-%02d_%04d.png", basename.c_str(), ipad, ++CHUNK_COUNTER[basename][ipad]);
						}else{
							// Use pad name in fname
							snprintf(fname_base, 256, "%s-%s_%04d.png", basename.c_str(), pad_name.c_str(), ++CHUNK_COUNTER[basename][ipad]);
						}
						char fname[512];
						snprintf(fname, 256, "%s/%s",  OUTPUT_DIR.c_str(), fname_base);

						// Get pad of interest
						if (pad) {

							// Determine crop parameters for this pad
							int x1 = pad->XtoAbsPixel(pad->GetX1());
							int x2 = pad->XtoAbsPixel(pad->GetX2());
							int y1 = pad->YtoAbsPixel(pad->GetY1());
							int y2 = pad->YtoAbsPixel(pad->GetY2());

							int w = x2 - x1;
							int h = y1 - y2; // yep, seems backwards doesn't it?

							// Read in image as TASImage object and crop it
							auto img = TASImage::Open( tmp_img_fname.c_str() );
							img->Crop(x1, y2, w, h);

							// Write cropped image to file
							cout << "    - Writing file " << fname << endl;
							img->WriteImage(fname, TImage::kPng);
							delete img;
							fnames_written.insert(fname);

							if( ! SYMLINK_DIR.empty() ){
								char fname_sym[256];
								snprintf( fname_sym, 256, "%s/%s", SYMLINK_DIR.c_str(), fname_base);
								cout << "    - Creating symlink " << fname << " <- " << fname_sym <<endl;
								symlink(fname, fname_sym);
							}
						}else{
							_DBG_ << "Unable to get pad " << ipad << " for " << hnamepath << " (for writing to " << fname << ")" << endl;
						}
					}
				}

				// Delete temporary image file
				unlink( tmp_img_fname.c_str() );
			}

			// Clear global
			rs_PadsToSave.clear();

		}
		RS_INFO->Unlock();

		// Run hydra
		if( ! fnames_written.empty() ){
			cout << "------- TODO: Run hydra on the following files: " << endl;
			for( auto f : fnames_written ) cout <<"-------       " << f << endl;
		}

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
	///    the string "rs_CheckAgainstAI"
	///
	/// 6. Send a request for the MACRO_HNAMEPATHS of all macros selected 
	///    in 5.
	///
	/// 7. Wait Twait seconds for them to respond.
	///
	/// Thus, by the time this routine returns a list of macros that may
	/// participate in the AI DQM will have been obtained
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

	// 2. Wait for 2 seconds
	sleep(2);
	
	// 3. Send a request for the macro definitions
	RS_INFO->Lock();
	for(auto &p : RS_INFO->histdefs){
		hdef_t &hdef = p.second;
		if( hdef.type == hdef_t::macro ){
			if( hdef.macro_hnamepaths.empty() ) { // try to only request each macro once
				if (VERBOSE > 1) cout << "Requesting macro " << hdef.hnamepath << " ..." << endl;
				if (RS_CMSG) RS_CMSG->RequestMacro(p.first, hdef.hnamepath);
				if (RS_XMSG) RS_XMSG->RequestMacro(p.first, hdef.hnamepath, true);
			}
		}
	}	
	RS_INFO->Unlock();
	if(VERBOSE>1) cout << "Waiting 2 seconds for servers to send macro definitions" << endl;

	// 4. Wait for 2 seconds
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

	// 5. Look through the macro definitions and find ones that contain "rs_CheckAgainstAI"
	RS_INFO->Lock();
	for(auto &p : RS_INFO->hinfos){
		string &macroString = p.second.macroString;
		if(macroString.find("rs_SavePad") != string::npos) MACRO_HNAMEPATHS.insert(p.second.hnamepath);
//		if( p.second.hnamepath == "//CDC_occupancy" ) _DBG_<< "============ " << p.second.hnamepath << endl << macroString << endl;
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
	
	// 7. Give servers Twait seconds to respond with histograms
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

	if(VERBOSE>1) cout << "Nservers: " << RS_INFO->servers.size() << "  -- hdefs: " << RS_INFO->histdefs.size() << "  -- hinfos: " << RS_INFO->hinfos.size() << endl;
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
	TStyle savestyle(*gStyle);
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
	*gStyle = savestyle;
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

	// allow for environmental variables
	const char *ptr = getenv("ROOTSPY_UDL");
	if(ptr)ROOTSPY_UDL = ptr;
	
	// check command line options
	static struct option long_options[] = {
		{"help",           no_argument,       0,  'h' },
		{"dir",            required_argument, 0,  'd' },
		{"link",           required_argument, 0,  'l' },
		{"run-number",     required_argument, 0,  'R' },
		{"poll-delay",     required_argument, 0,  'p' },
		{"udl",            required_argument, 0,  'u' },
		{"server",         required_argument, 0,  's' },
		// {"session-name",   required_argument, 0,  'S' },
		{"min-rsai-events",required_argument, 0,  'm' },
		{"verbose",        no_argument,       0,  'V' },
		
		{0, 0, 0, 0  }
	};
	
	int opt = 0;
	int long_index = 0;
	while ((opt = getopt_long(narg, argv,"hd:l:R:p:u:s:m:v",
							  long_options, &long_index )) != -1) {
		switch (opt) {
			case 'R':
				if(optarg == NULL) Usage();
				RUN_NUMBER = atoi(optarg);
				break;
			case 'd' :
				if(optarg == NULL) Usage();
				OUTPUT_DIR = optarg;
				break;
			case 'l' :
				if(optarg == NULL) Usage();
				SYMLINK_DIR = optarg;
				break;
			// case 'f' :
			// 	FORCE_START = true;
			// 	break;
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
			// case 'S' :
			// 	SESSION = optarg;
			// 	break;
			case 'm' :
				MIN_EVENTS_RSAI = atoi(optarg);
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
	cout << "      OUTPUT_DIR = " << OUTPUT_DIR << endl;
	cout << "     SYMLINK_DIR = " << (SYMLINK_DIR.empty() ? "<none>":SYMLINK_DIR) << endl;
	cout << "     ROOTSPY_UDL = " << ROOTSPY_UDL << endl;
	// cout << "         SESSION = " << SESSION << endl;
	cout << "      RUN_NUMBER = " << RUN_NUMBER << endl;
	cout << "      POLL_DELAY = " << POLL_DELAY << endl;
	cout << "  MIN_POLL_DELAY = " << MIN_POLL_DELAY << endl;
	
	cout << "-------------------------------------------------" << endl;

	// Check if output directories exist and if not, try to make them.
	// Note that in the GlueX online, these directories should be made
	// already by the start_monitoring script.
	auto *dir = opendir(OUTPUT_DIR.c_str());
	if( dir == NULL ) {
		cout << "Output directory does not exist: " << OUTPUT_DIR << endl;
		if( mkdir( OUTPUT_DIR.c_str(), 0775 ) ) {
			cout << "ERROR: Unable to create output directory: " << OUTPUT_DIR << endl;
		}else{
			cout << "Output directory created: " << OUTPUT_DIR << endl;
		}
	}else{
		closedir( dir );
	}

	if( !SYMLINK_DIR.empty() ){
		dir = opendir(SYMLINK_DIR.c_str());
		if( dir == NULL ) {
			cout << "Symlink directory does not exist: " << SYMLINK_DIR << endl;
			if( mkdir( SYMLINK_DIR.c_str(), 0775 ) ) {
				cout << "ERROR: Unable to create symlink directory: " << SYMLINK_DIR << endl;
			}else{
				cout << "Symlink directory created: " << SYMLINK_DIR << endl;
			}
		}else{
			closedir( dir );
		}
	}
}


//-----------
// Usage
//-----------
void Usage(void)
{
	cout<<"Usage:"<<endl;
	cout<<"       RSAI [options]"<<endl;
	cout<<endl;
	cout<<"Saves ROOT histograms being stored by programs running the RootSpy plugin."<<endl;
	cout<<endl;
	cout<<"Options:"<<endl;
	cout<<endl;
	cout<<"   -h,--help                 Print this message"<<endl;
	cout<<"   -u,--udl udl              UDL of cMsg RootSpy server"<<endl;
	cout<<"   -d,--dir output-dir       Output directory for image files"<<endl;
	cout<<"   -l,--link symlink-dir     Create symlinks to image files in this directory"<<endl;
	cout<<"   -s,--server server-name   Set RootSpy UDL to point to server IP/hostname"<<endl;
	cout<<"   -R,--run-number number    The number of the current run" << endl;
	cout<<"   -p,--poll-delay time      Time (in seconds) to wait between polling seconds" << endl;
	cout<<"   -m,--min-rsai-events N    Sets event scale used by macros to decided how often to run (def. " << MIN_EVENTS_RSAI <<")" << endl;
	// cout<<"   -S,--session-name session Set session name (unused)" << endl;
	cout<<"   -v,--verbose              Increase verbosity (can use multiple times)"<<endl;
	cout<<endl;
	cout<<endl;
	
	exit(0);
}

