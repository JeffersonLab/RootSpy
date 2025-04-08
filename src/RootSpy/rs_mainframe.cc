
#include "RootSpy.h"
#include "rs_mainframe.h"
#include "rs_info.h"
#include "rs_cmsg.h"
#include "rs_macroutils.h"
#include "Dialog_SelectHists.h"
#include "Dialog_SaveHists.h"
#include "Dialog_IndivHists.h"
#include "Dialog_SelectTree.h"
#include "Dialog_ConfigMacros.h"
#include "Dialog_ScaleOpts.h"

#include <TROOT.h>
#include <TSystem.h>
#include <TStyle.h>
#include <TApplication.h>
#include <TInterpreter.h>
#include <TPolyMarker.h>
#include <TLine.h>
#include <TMarker.h>
#include <TBox.h>
#include <TVector3.h>
#include <TGeoVolume.h>
#include <TGeoManager.h>
#include <TGLabel.h>
#include <TGComboBox.h>
#include <TGButton.h>
#include <TGButtonGroup.h>
#include <TGTextEntry.h>
#include <TBrowser.h>
#include <TArrow.h>
#include <TLatex.h>
#include <TColor.h>
#include <TGFileDialog.h>
#include <TFile.h>
#include <TGaxis.h>
#include <TFrame.h>
#include <TFileMerger.h>
#include <TKey.h>
#include <THashList.h>
#include <TFileMergeInfo.h>
#include <TGInputDialog.h>
#include <TGMsgBox.h>

#include <KeySymbols.h>

#include <unistd.h>
#include <sys/stat.h>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <fstream>
#include <thread>
#include <mutex>
using namespace std;

#ifdef HAVE_EZCA
//#include <tsDefs.h> 
#include <cadef.h> 
#include <ezca.h>
#endif // HAVE_EZCA


extern string ROOTSPY_UDL;
extern string CMSG_NAME;

extern string ELOG_NAME;
extern string ELOG_EMAIL;
extern bool   ELOG_NOTIFY;

// These defined in rs_cmsg.cc
extern mutex REGISTRATION_MUTEX_CMSG;
extern map<void*, string> HISTOS_TO_REGISTER_CMSG;
extern map<void*, string> MACROS_TO_REGISTER_CMSG;

// These defined in rs_xmsg.cc
extern mutex REGISTRATION_MUTEX_XMSG;
extern set<rs_serialized*> HISTOS_TO_REGISTER_XMSG;
extern set<rs_serialized*> MACROS_TO_REGISTER_XMSG;

// information for menu bar
enum MenuCommandIdentifiers {
  M_FILE_OPEN,
  M_FILE_SAVE,
  M_FILE_NEW_CONFIG,
  M_FILE_OPEN_CONFIG,
  M_FILE_SAVE_CONFIG,
  M_FILE_SAVE_AS_CONFIG,
  M_FILE_EXIT,

  M_TOOLS_MACROS,
  M_TOOLS_TBROWSER,
  M_TOOLS_TREEINFO,
  M_TOOLS_SAVEHISTS,
  M_TOOLS_RESET,

  M_VIEW_NEW_TAB,
  M_VIEW_REMOVE_TAB,
  M_VIEW_LOGX,
  M_VIEW_LOGY,
  M_VIEW_SCALE_OPTS

};

// static functions
//static Bool_t MergeMacroFiles(TDirectory *target, vector<TFile *> &sourcelist);
static Bool_t MergeMacroFiles(TDirectory *target, TList *sourcelist);

//-------------------
// Constructor
//-------------------
rs_mainframe::rs_mainframe(const TGWindow *p, UInt_t w, UInt_t h,  bool build_gui, string startup_config_filename=""):TGMainFrame(p,w,h, kMainFrame | kVerticalFrame),config_filename(startup_config_filename)
{
	current_tab = NULL;
	run_number_label = NULL;
	//config_filename = "";

	//Define all of the -graphics objects. 
	if(build_gui) {
	    CreateGUI();
		
		ReadPreferences();
		if(config_filename != "") ReadConfig(config_filename);
		
		// make sure we have at least one tab
		if(rstabs.empty()) new RSTab(this, "New");

	    // Set up timer to call the DoTimer() method repeatedly
	    // so events can be automatically advanced.
	    timer = new TTimer();
	    timer->Connect("Timeout()", "rs_mainframe", this, "DoTimer()");
	    sleep_time = 250;
	    timer->Start(sleep_time, kFALSE);
	}

	double now = rs_cmsg::GetTime();
	last_called=now - 1.0;
	last_ping_time = now;
	last_hist_requested = -4.0;
	selected_tab_from_prefrences = 0;

	last_requested.hnamepath = "N/A";
	last_hist_plotted = NULL;
	
	dialog_configmacros = NULL;
	dialog_scaleopts = NULL;
	dialog_referenceplot = NULL;
	dialog_newreference = NULL;
	delete_dialog_configmacros = false;

	//overlay_mode = false;
	archive_file = NULL;
	
	// These are used when generating plots to make an e-log entry
	elog_timer = NULL;
	Npages_elog = 0;
	ipage_elog  = 0;
	epics_run_number = 0;

	//overlay_mode = true;
	//archive_file = new TFile("/u/home/sdobbs/test_archives/run1_output.root");

	SetWindowName("RootSpy");
	SetIconName("RootSpy");

	MapSubwindows();
	Resize(GetDefaultSize());
	MapWindow();

	viewStyle_rs = kViewByServer_rs;
	exec_shell = new TExec();

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
	gROOT->ProcessLine("extern \"C\" { void rs_SavePad(const string fname, int ipad){} }"); // disable this for RootSpy GUI (it's meant for RSAI)
	gROOT->ProcessLine("extern \"C\" { void InsertSeriesData(string sdata){} }"); // disable insertion of series data for RootSpy GUI
	gROOT->ProcessLine("extern \"C\" { void InsertSeriesMassFit(string ptype, double mass, double width, double mass_err, double width_err, double unix_time=0.0){} }"); // (same as previous)

	rs_SetFlag("RESET_AFTER_FIT", 0);     // ensure histograms are not automatically reset by macros supporting RSTimeSeries
	rs_SetFlag("RESET_AFTER_SAVEPAD", 0); // ensure histograms are not automatically reset by macros supporting RSAI

	// The following ensures that the routines in rs_macroutils are
	// linked in to the final executable
	if(rs_GetFlag("RESET_AFTER_FIT")){
		// (should never actually get here)
		rs_ResetHisto("N/A");
		rs_RestoreHisto("N/A");
		rs_SavePad("N/A", 0);
	}

#ifdef HAVE_EZCA
	// Optionally try and get run number
	string epics_var_name = "HD:coda:daq:run_number";
	ezcaGet((char*)(epics_var_name.c_str()), ezcaLong, 1, &epics_run_number);
        _DBG_ << "EPICS run number: " << epics_run_number << endl;
        last_epics_run_number_checked = now;
#endif // HAVE_EZCA
}

//-------------------
// Destructor
//-------------------
rs_mainframe::~rs_mainframe(void)
{
	if(run_number_label) delete run_number_label;
}

//-------------------
// CloseWindow
//-------------------
void rs_mainframe::CloseWindow(void)
{
	DeleteWindow();
	gApplication->Terminate(0);
}

//-------------------
// SavePreferences
//-------------------
void rs_mainframe::SavePreferences(void)
{
	// Preferences file is "${HOME}/.RootSys"
	const char *home = getenv("HOME");
	if(!home)return;
	
	// Try deleting old file and creating new file
	string fname = string(home) + "/.rootspy";
	unlink(fname.c_str());
	ofstream ofs(fname.c_str());
	if(!ofs.is_open()){
		cout<<"Unable to create preferences file \""<<fname<<"\"!"<<endl;
		return;
	}
	
	// Write header
	time_t t = time(NULL);
	ofs<<"##### RootSys preferences file ###"<<endl;
	ofs<<"##### Auto-generated on "<<ctime(&t)<<endl;
	ofs<<endl;

	// Global settings
	ofs<<"config-file \"" << config_filename << "\"" << endl;
	ofs<<"window-size " << GetWidth() << " " << GetHeight() << endl;
	ofs<<"auto-refresh " << (bAutoRefresh->GetState()==kButtonDown ? "on":"off") << endl;
	ofs<<"auto-advance " << (bAutoAdvance->GetState()==kButtonDown ? "on":"off") << endl;
	ofs<<"delay        " << delay_time << endl;
	// ofs<<"selected-tab " << fMainTab->GetCurrent() << endl;
        ofs<<"selected-tab 0" << endl;
	ofs<<endl;

	ofs<<endl;
	ofs.close();
	cout<<"Preferences written to \""<<fname<<"\""<<endl;
}

//-------------------
// ReadPreferences
//-------------------
void rs_mainframe::ReadPreferences(void)
{
	// Preferences file is "${HOME}/.RootSys"
	const char *home = getenv("HOME");
	if(!home)return;

	// Read in file, spliting it into section and tokenizing it
	string fname = string(home) + "/.rootspy";
	cout<<"Reading preferences from \""<<fname<<"\" ..."<<endl;
	map<string, vector<config_item_t> > config_items;
	TokenizeFile(fname, config_items);

	// Loop over config items
	map<string, vector<config_item_t> >::iterator iter;
	for(iter=config_items.begin(); iter!=config_items.end(); iter++){
		
		// For convenience, setup references to type and first element's tokens
		string type = iter->first;
		config_item_t &ci = iter->second[0];
		vector<string> &tokens = ci.tokens;

		if(type == "config-file"){
			if(tokens.size()>1){
				if(config_filename == "")
					config_filename = tokens[1];
				
			}
		}
		if(type == "window-size"){
			if(tokens.size()>2){
				UInt_t w = atoi(tokens[1].c_str());
				UInt_t h = atoi(tokens[2].c_str());
				winsize_from_preferences = TGDimension(w, h);
				TTimer::SingleShot(500, "rs_mainframe", this, "DoSetWindowSize()");
			}
		}
		if(type == "auto-refresh"){
			if(tokens.size()>1){
				bAutoRefresh->SetOn(tokens[1] == "on");
			}else{
				bAutoRefresh->SetOn(kTRUE);
			}
		}
		if(type == "auto-advance"){
			if(tokens.size()>1){
				bAutoAdvance->SetOn(tokens[1] == "on");
			}else{
				bAutoAdvance->SetOn(kTRUE);
			}
		}
		if(type == "delay"){
			if(tokens.size()>1){
				delay_time = atoi(tokens[1].c_str());
				fDelay->Select(delay_time);
				char str[8];
				sprintf(str, "%lds", delay_time);
				fDelay->GetTextEntry()->SetText(str);
			}
		}
		if(type == "selected-tab"){
			if(tokens.size()>1){
				selected_tab_from_prefrences = atoi(tokens[1].c_str());
			}
		}
	}
}

//-------------------
// SaveConfigAs
//-------------------
void rs_mainframe::SaveConfigAs(void)
{
	// Temporarily set config_filename to empty string
	// in order to force SaveConfig to present a dialog
	string save_config_filename = config_filename;
	config_filename = "";

	SaveConfig();
	
	// No config file was saved, (user probably hit cancel). Revert to previous.
	if(config_filename=="") config_filename = save_config_filename;
}

//-------------------
// SaveConfig
//-------------------
void rs_mainframe::SaveConfig(void)
{
	_DBG_ << "In rs_mainframe::SaveConfig()..." << endl;

	// If config_filename is not set, then ask user for one
	if(config_filename == ""){
		TGFileInfo file_info;
		const char *filetypes[] = {"RootSpy config. files", "*.rsconfig", "All files", "*", 0, 0};
		file_info.fFileTypes = filetypes;
		file_info.fMultipleSelection = false;
		file_info.fOverwrite = true;
		file_info.fFilename = (char*)malloc(1024);
		file_info.fFilename[0] = '\0';
		file_info.fIniDir = (char*)malloc(1024);
		file_info.fIniDir[0] = '\0';
		new TGFileDialog(gClient->GetRoot(), this, kFDSave, &file_info);
		if(!file_info.fFilename) return; // user hit "cancel"

		// The filename returned by the dialog box is the full pathname,
		// so we don't need the directory name
		string filename = file_info.fFilename;
		//string dirname  = file_info.fIniDir;
		//config_filename = dirname + filename;
		config_filename = filename; 
		// sanity check that it's actually a pathname, though
		if( (config_filename.length() < 1) || (config_filename == "" ) )
			return;
		if( config_filename[0] != '/' ) 
			config_filename = "/" + config_filename;
		if(config_filename.length() < 9){
			config_filename += ".rsconfig";
		}else{
			if(config_filename.substr(config_filename.length()-9) != ".rsconfig"){
				config_filename += ".rsconfig";
			}
		}
	}
	
	// Try deleting old file and creating new file
	unlink(config_filename.c_str());
	ofstream ofs(config_filename.c_str());
	if(!ofs.is_open()){
		cout<<"Unable to create preferences file \""<<config_filename<<"\"!"<<endl;
		return;
	}
	
	// Write header
	time_t t = time(NULL);
	ofs<<"##### RootSys config. file ###"<<endl;
	ofs<<"##### Generated on "<<ctime(&t)<<endl;
	ofs<<endl;

	// Global settings
	ofs<<endl;
	
	// Tabs
	list<RSTab*>::iterator tab_it = rstabs.begin();
	for(; tab_it!=rstabs.end(); tab_it++){
		RSTab *rstab = *tab_it;

		ofs<<"TAB: \"" << rstab->title << "\" " << rstab->currently_displayed << endl;
		list<string>::iterator h_it = rstab->hnamepaths.begin();
		for(; h_it!= rstab->hnamepaths.end(); h_it++){
			ofs << *h_it << endl;
		}
		ofs<<endl;
	}
	ofs<<endl;

	ofs<<endl;
	ofs.close();
	cout<<"Configuration written to \""<<config_filename<<"\""<<endl;
}

//-------------------
// ReadConfig
//-------------------
void rs_mainframe::ReadConfig(string fname)
{
	// If fname is not set, then ask user for one
	if(fname == ""){
		TGFileInfo file_info;
		const char *filetypes[] = {"RootSpy config. files", "*.rsconfig", "All files", "*", 0, 0};
		file_info.fFileTypes = filetypes;
		file_info.fMultipleSelection = false;
		file_info.fOverwrite = true;
		file_info.fFilename = (char*)malloc(512);
		file_info.fIniDir = (char*)malloc(512);
		file_info.fFilename[0] = file_info.fIniDir[0] = 0;
		new TGFileDialog(gClient->GetRoot(), this, kFDOpen, &file_info);
		if(!file_info.fFilename) return; // user hit "cancel"

		string filename = file_info.fFilename;
		string dirname  = file_info.fIniDir;
		fname = filename;
	}
	
	cout << "Reading configuration from \""<<fname<<"\""<<endl;
	config_filename = fname;

	// Read in file, spliting it into section and tokenizing it
	map<string, vector<config_item_t> > config_items;
	if(TokenizeFile(fname, config_items)) return;

	// Remove all existing tabs
	int Ntabs = fMainTab->GetNumberOfTabs();
	for(int i=0; i<Ntabs; i++) fMainTab->RemoveTab(Ntabs-1 - i);
	list<RSTab*>::iterator tab_iter = rstabs.begin();
	for(; tab_iter!=rstabs.end(); tab_iter++) delete *tab_iter;
	rstabs.clear();
	
	// Get TAB configurations and create new tabs
	map<string, vector<config_item_t> >::iterator iter = config_items.find("TAB:");
	if(iter != config_items.end()){
		vector<config_item_t> &config_items = iter->second;
		for(uint32_t i=0; i<config_items.size(); i++){

			config_item_t &ci = config_items[i];
			vector<string> &tokens = ci.tokens;
			vector<string> &subitems = ci.subitems;
			
			if(tokens.size()>1){
				string title = tokens[1];
				RSTab *rstab = new RSTab(this, title);
				
				// Add hnamepaths
				for(uint32_t j=0; j<subitems.size(); j++){
					rstab->hnamepaths.push_back(subitems[j]);
				}
				
				// Set currently displayed histogram
				if(tokens.size()>2){
					rstab->currently_displayed = atoi(tokens[2].c_str());
				}
				
				// Launch thread to try and preload hists and macros
				// thread t(&rs_cmsg::SeedHnamepaths, RS_CMSG, rstab->hnamepaths, true, true);
				// t.detach();
				
				rstab->DoUpdateWithFollowUp();
			}
		}
	}

	// Set the tab read from the preferences file
	if( (selected_tab_from_prefrences>=0) && (selected_tab_from_prefrences<fMainTab->GetNumberOfTabs())){
		fMainTab->SetTab(selected_tab_from_prefrences);
		DoTabSelected(selected_tab_from_prefrences);
	}

	// Screen does not update correctly without these!
	TGDimension dim(GetWidth()+1, GetHeight()); // size must be different than current!
	Resize(dim); // (does not actually change size)
	MapSubwindows();
}

//-------------------
// TokenizeFile
//-------------------
bool rs_mainframe::TokenizeFile(string fname, map<string, vector<config_item_t> > &config_items)
{
	ifstream ifs(fname.c_str());
	if(!ifs.is_open()){
		cout << "unable to open file: " << fname << endl;
		return true;
	}

	// Loop over lines
	config_item_t *filling_config = NULL;
	char line[1024];
	while(!ifs.eof()){
		ifs.getline(line, 1024);
		if(strlen(line)==0){ filling_config=NULL; continue; }
		if(line[0] == '#')continue;
		//string str(line);
		
		// If filling a multi-line config item just add whole line and continue
		if(filling_config){
			filling_config->subitems.push_back(line);
			continue;
		}

		// Break line into tokens, respecting double quotes
		vector<string> tokens;
		uint32_t pos=0;
		bool in_quotes = false;
		bool in_token = false;
		string token;
		while(pos<strlen(line)){
			if(isspace(line[pos])){
				if(in_token){
					if(!in_quotes){
						tokens.push_back(token);
						token = "";
						in_token = false;
					}else{
						token += line[pos];
					}
				}
			}else if(line[pos]=='"'){
				if(in_quotes){
					// end quote
					tokens.push_back(token);
					token = "";
					in_token = in_quotes = false;
				}else{
					// start quote
					in_token = in_quotes = true;
				}
			}else{
				token += line[pos];
				in_token = true;
			}
			pos++;
		}
		if(in_token) tokens.push_back(token);
		
		// Create a new configuration item. If the first token ends
		// with ":" then set the filling_config pointer to it
		if(!tokens.empty()){
			string &type = tokens[0];
			if(!type.empty()){
				config_item_t ci;
				ci.type = type;
				ci.tokens = tokens;
				config_items[ci.type].push_back(ci);
				if(type[type.size()-1] == ':'){
					vector<config_item_t> &vci = config_items[ci.type];
					filling_config = &vci[vci.size()-1];
				}
			}
		}
	}
	
	// close file
	ifs.close();

	return false;
}

//-------------------
// HandleKey
//-------------------
Bool_t rs_mainframe::HandleKey(Event_t *event)
{
  // Handle keyboard events.

  char   input[10];
   UInt_t keysym;

  cerr << "in HandleKey()..." << endl;

  if (event->fType == kGKeyPress) {
    gVirtualX->LookupString(event, input, sizeof(input), keysym);

    cerr << " key press!" << endl;

    /*
    if (!event->fState && (EKeySym)keysym == kKey_F5) {
      Refresh(kTRUE);
      return kTRUE;
    }
    */
    switch ((EKeySym)keysym) {   // ignore these keys
    case kKey_Shift:
    case kKey_Control:
    case kKey_Meta:
    case kKey_Alt:
    case kKey_CapsLock:
    case kKey_NumLock:
    case kKey_ScrollLock:
      return kTRUE;
    default:
      break;
    }


    if (event->fState & kKeyControlMask) {   // Cntrl key modifier pressed
      switch ((EKeySym)keysym & ~0x20) {   // treat upper and lower the same
      case kKey_X:
	fMenuFile->Activated(M_FILE_EXIT);
	return kTRUE;
      default:
	break;
      }
    }
  }
  return TGMainFrame::HandleKey(event);
}

//-------------------
// HandleMenu
//-------------------
void rs_mainframe::HandleMenu(Int_t id)
{
   // Handle menu items.

  //cout << "in HandleMenu(" << id << ")" << endl;

   switch (id) {

   case M_FILE_OPEN:
     DoLoadHistsList();
     break;

   case M_FILE_SAVE:
     DoSaveHistsList();
     break;
	
   case M_FILE_NEW_CONFIG:
     break;

   case M_FILE_OPEN_CONFIG:
     ReadConfig();
     break;

   case M_FILE_SAVE_CONFIG:
     SaveConfig();
     break;

   case M_FILE_SAVE_AS_CONFIG:
     SaveConfigAs();
     break;

   case M_FILE_EXIT: 
     DoQuit();       
     break;

   case M_TOOLS_MACROS:
     DoConfigMacros();
     break;

   case M_TOOLS_TBROWSER:
     DoMakeTB();
     break;

   case M_TOOLS_TREEINFO:
     DoTreeInfo();
     break;

   case M_TOOLS_SAVEHISTS:    
     DoSaveHists();
     break;

   case M_TOOLS_RESET:
       DoResetDialog();
       break;

   case M_VIEW_NEW_TAB:
       DoNewTabDialog();
	   break;

   case M_VIEW_REMOVE_TAB:
       DoRemoveTabDialog();
	   break;

   case M_VIEW_LOGX:
   case M_VIEW_LOGY:
       DoSetViewOptions(id);
       break;

   case M_VIEW_SCALE_OPTS:
       DoSetScaleOptions();
       break;
   }
}

//-------------------
// DoQuit
//-------------------
void rs_mainframe::DoQuit(void)
{
	cout<<"quitting ..."<<endl;
	SavePreferences();

	// This is supposed to return from the Run() method in "main()"
	// since we call SetReturnFromRun(true), but it doesn't seem to work.
	//gApplication->SetReturnFromRun(true);
	cout << "DoQuit called." << endl;
	
	gApplication->Terminate(0);
	//gSystem->ExitLoop();
}

//-------------------
// DoMakeTB
//-------------------
void rs_mainframe::DoMakeTB(void)
{
	cout<<"Making new TBrowser"<<endl;
	new TBrowser();
		
	//Outputs a new TBrowser, which will help with DeBugging.
}

//-------------------
// DoSetWindowSize
//-------------------
void rs_mainframe::DoSetWindowSize(void)
{
	/// This is called via a timer setup while reading in preferences.
	/// The timer is use to delay this breifly while the window is mapped.
	Resize(winsize_from_preferences);
}

//-------------------
// DoResetDialog
//-------------------
void rs_mainframe::DoResetDialog(void)
{
	Int_t ret_code = 0;
	new TGMsgBox(gClient->GetRoot(), this, "Reset Histograms", "Are you sure you want to delete all\nhistograms and trees? (will not affect\nremote processes)", kMBIconQuestion, kMBOk|kMBCancel, &ret_code);
	if(ret_code == kMBOk) RS_INFO->Reset();
}

//-------------------
// DoSetScaleOptions
//-------------------
void rs_mainframe::DoSetScaleOptions(void)
{
	string hnamepath = "";
	if(current_tab){
		list<string>::iterator it = current_tab->hnamepaths.begin();
		advance(it, current_tab->currently_displayed);
		hnamepath = *it;
	}
	if(hnamepath=="") return;
	if(!dialog_scaleopts){
		dialog_scaleopts = new Dialog_ScaleOpts(hnamepath, gClient->GetRoot(), 350, 200);
	}else{
		dialog_scaleopts->RaiseWindow();
		dialog_scaleopts->RequestFocus();
	}
}

//-------------------
// DoNewTabDialog
//-------------------
void rs_mainframe::DoNewTabDialog(void)
{
	char retstr[256] = "";
	new TGInputDialog(gClient->GetRoot(), this, "New Tab Name", "NewTab", retstr);
	if(strlen(retstr) > 0){
		new RSTab(this, retstr);
		
		// Screen does not update correctly without these!
		Resize(); // (does not actually change size)
		MapSubwindows();
	}
}

//-------------------
// DoRemoveTabDialog
//-------------------
void rs_mainframe::DoRemoveTabDialog(void)
{
	Int_t id = fMainTab->GetCurrent();
	if(id<0 || id>=fMainTab->GetNumberOfTabs()){
		new TGMsgBox(gClient->GetRoot(), this, "Remove Tab", "No tab to remove!", kMBIconExclamation);
		return;
	}
	
	Int_t ret_code = 0;
	new TGMsgBox(gClient->GetRoot(), this, "Remove Tab", "Are you sure you want to\nremove the current tab?", kMBIconQuestion, kMBOk|kMBCancel, &ret_code);
	if(ret_code == kMBOk){
		fMainTab->RemoveTab(id);
		list<RSTab*>::iterator it = rstabs.begin();
		advance(it, id);
		rstabs.erase(it);
		delete *it;
		current_tab = rstabs.empty() ? NULL:rstabs.front();
		MapSubwindows();
	}
}

//-------------------
// DoTabSelected
//-------------------
void rs_mainframe::DoTabSelected(Int_t id)
{
	list<RSTab*>::iterator it = rstabs.begin();
	advance(it, id);
	current_tab = *it;
	current_tab->DoUpdateWithFollowUp();
}

//-------------------
// DoSaveHists
//-------------------
void rs_mainframe::DoSaveHists(void)
{

	if(!dialog_savehists){
		dialog_savehists = new Dialog_SaveHists(gClient->GetRoot(), 10, 10);
	}else{
		dialog_savehists->RaiseWindow();
		dialog_savehists->RequestFocus();
	}
}

//-------------------
// DoTreeInfo
//-------------------
void rs_mainframe::DoTreeInfo(void) {
	if(!dialog_selecttree){
		dialog_selecttree = new Dialog_SelectTree(gClient->GetRoot(), 10, 10);
	}else{
		dialog_selecttree->RaiseWindow();
		dialog_selecttree->RequestFocus();
	}
}

//-------------------
// DoConfigMacros
//-------------------
void rs_mainframe::DoConfigMacros(void) {
	if(!dialog_configmacros){
		dialog_configmacros = new Dialog_ConfigMacros(gClient->GetRoot(), 10, 10);
	}else{
		dialog_configmacros->RaiseWindow();
		dialog_configmacros->RequestFocus();
	}
}

//-------------------
// DoSelectDelay
//-------------------
void rs_mainframe::DoSelectDelay(Int_t index)
{
	_DBG_<<"Setting auto-refresh delay to "<<index<<"seconds"<<endl;
	delay_time = (time_t)index;
}

//-------------------
// DoTimer
//-------------------
void rs_mainframe::DoTimer(void) {
	/// This gets called periodically (value is set in constructor)
	/// It's main job is to communicate with the callback through
	/// data members more or less asynchronously.

	// The following made the "View Indiv." button behave poorly
	// (not respond half the time it was clicked.) I've disabled this
	// for now to get it to behave better.  9/16/2010  D.L.
	//
	//Set indiv button enabled or disabled, added justin b.
	//if (can_view_indiv) indiv->SetEnabled(kTRUE);
	//else indiv->SetEnabled(kFALSE);

  /*
  // disable whole timer routine if we're not connected to cMsg? - sdobbs, 4/22/2013
        if(!RS_CMSG->IsOnline())
	    return;
  */
	double now = 0.0;
	if( RS_CMSG ) now = RS_CMSG->GetTime();
	if( RS_XMSG ) now = RS_XMSG->GetTime();
	
	// Pings server to keep it alive
	if(now-last_ping_time >= 3.0){
		if( RS_CMSG ) RS_CMSG->PingServers();
		if( RS_XMSG ) RS_XMSG->PingServers();
		last_ping_time = now;
	}
	
	// Ask for list of all hists and macros from all servers every 5 seconds
	if(now-last_hist_requested >= 5.0){
		if( RS_CMSG ) RS_CMSG->RequestHists("rootspy");
		if( RS_CMSG ) RS_CMSG->RequestMacroList("rootspy");
		if( RS_XMSG ) RS_XMSG->RequestHists("rootspy");
		if( RS_XMSG ) RS_XMSG->RequestMacroList("rootspy");
		last_hist_requested = now;
	}

	// Handle auto-update and auto-refresh
	if(bAutoRefresh->GetState()==kButtonDown){
		if(current_tab){
			double tdiff = now - current_tab->last_request_sent;
			if( tdiff >= (double)delay_time ){

				if(bAutoAdvance->GetState()==kButtonDown){
					current_tab->DoNext();
				}else{
					current_tab->DoUpdate();
				}
			}
		}
	}

	// Register any histograms waiting in the cMsg queue
	if( RS_CMSG ) { 
		if( ! HISTOS_TO_REGISTER_CMSG.empty() ){
			REGISTRATION_MUTEX_CMSG.lock();
			for(auto h : HISTOS_TO_REGISTER_CMSG){
				RS_CMSG->RegisterHistogram(h.second, (cMsgMessage*)h.first, true);
			}
			HISTOS_TO_REGISTER_CMSG.clear();
			REGISTRATION_MUTEX_CMSG.unlock();
		}

		// Register any macros waiting in the queue
		if( ! MACROS_TO_REGISTER_CMSG.empty() ){
			REGISTRATION_MUTEX_CMSG.lock();
			for(auto m : MACROS_TO_REGISTER_CMSG){
				RS_CMSG->RegisterMacro(m.second, (cMsgMessage*)m.first);
			}
			MACROS_TO_REGISTER_CMSG.clear();
			REGISTRATION_MUTEX_CMSG.unlock();
		}

		// If reference window is up, then update it
		if(dialog_referenceplot){
			if(dialog_referenceplot->IsMapped()){
				((Dialog_ReferencePlot*)dialog_referenceplot)->DoTimer();
			}
		}
	}

	// Register any histograms waiting in the xMsg queue
	if( RS_XMSG ) { 
		if( ! HISTOS_TO_REGISTER_XMSG.empty() ){
			REGISTRATION_MUTEX_XMSG.lock();
			for(auto h : HISTOS_TO_REGISTER_XMSG){
				RS_XMSG->RegisterHistogram(h);
				delete h;
			}
			HISTOS_TO_REGISTER_XMSG.clear();
			REGISTRATION_MUTEX_XMSG.unlock();
		}

		// Register any macros waiting in the queue
		if( ! MACROS_TO_REGISTER_XMSG.empty() ){
			REGISTRATION_MUTEX_XMSG.lock();
			for(auto m : MACROS_TO_REGISTER_XMSG){
				RS_XMSG->RegisterMacro(m);
				delete m;
			}
			MACROS_TO_REGISTER_XMSG.clear();
			REGISTRATION_MUTEX_XMSG.unlock();
		}

		// If reference window is up, then update it
		if(dialog_referenceplot){
			if(dialog_referenceplot->IsMapped()){
				((Dialog_ReferencePlot*)dialog_referenceplot)->DoTimer();
			}
		}
	}
#ifdef HAVE_EZCA
	// Optionally try and get run number every 60 seconds
	if( (now-last_epics_run_number_checked) >= 60 ){
		string epics_var_name = "HD:coda:daq:run_number";
		long old_run = epics_run_number;
		if(EZCA_OK == ezcaGet((char*)(epics_var_name.c_str()), ezcaLong, 1, &epics_run_number)){
			if(old_run != epics_run_number){
				cout << "Run number change sensed from " << old_run << " to " << epics_run_number << endl;
				DropAllHists();
			}
		}
		last_epics_run_number_checked = now;
	}
#endif // HAVE_EZCA

	last_called = now;
}

//----------
// DoSetArchiveFile
//----------
//add comment
void rs_mainframe::DoSetArchiveFile(void) {
	TGFileInfo* fileinfo = new TGFileInfo();
	new TGFileDialog(gClient->GetRoot(), gClient->GetRoot(), kFDOpen, fileinfo);
	if(!fileinfo->fFilename) return; // user hit cancel

	if(archive_file) {
	    archive_file->Close();
	    delete archive_file;
	}

	archive_file = new TFile(fileinfo->fFilename);
	// check for errors?

	// update display
	char *fullpathname = realpath(fileinfo->fFilename,NULL);
	if(fullpathname==NULL) {
		fullpathname = (char*)malloc(10);  // get more space than we need
		fullpathname[0] = '\0';
	}	
	string labeltext = "Archive = " + string(fullpathname);
	lArchiveFile->SetTitle(labeltext.c_str());
	lArchiveFile->Draw();
	
	cout << "Opened ROOT file = " << fullpathname << endl;

	free(fullpathname);
}

//-------------------
// DoTreeInfoShort
//-------------------
void rs_mainframe::DoTreeInfoShort(void) {
	RS_INFO->Lock();
	map<string,server_info_t>::iterator iter = RS_INFO->servers.begin();
	for(; iter!=RS_INFO->servers.end(); iter++){
		string servername = iter->first;
		if(servername!=""){
			if( RS_CMSG ) RS_CMSG->RequestTreeInfo(servername);
		}
	}
	RS_INFO->Unlock();
}

//-------------------
// DoSetViewOptions
//-------------------
void rs_mainframe::DoSetViewOptions(int menu_item)
{
    cout << "In rs_mainframe::DoSetViewOptions()..." << endl;  
    cout << "  menu item = " << menu_item << endl;

    RS_INFO->Lock();

    // make sure that there is a valid histogram loaded
    map<string,hdef_t>::iterator hdef_itr = RS_INFO->histdefs.find(RS_INFO->current.hnamepath);
    if( (RS_INFO->current.hnamepath == "")
	|| (hdef_itr == RS_INFO->histdefs.end()) ) {
	RS_INFO->Unlock();
	return;
    }
	
    // save the option info
    switch(menu_item) {
    case M_VIEW_LOGX:
	if(hdef_itr->second.display_info.use_logx)
	    hdef_itr->second.display_info.use_logx = false;
	else
	    hdef_itr->second.display_info.use_logx = true;
	break;
    case M_VIEW_LOGY:
	if(hdef_itr->second.display_info.use_logx)
	    hdef_itr->second.display_info.use_logy = false;
	else
	    hdef_itr->second.display_info.use_logy = true;
	break;
    }
    

    // update the GUI
    // there should be some better way to do this?
    if(fMenuView->IsEntryChecked(menu_item))
	fMenuView->UnCheckEntry(menu_item);
    else
	fMenuView->CheckEntry(menu_item);

    RS_INFO->Unlock();
}

//-------------------
// DoUpdateViewMenu
//-------------------
void rs_mainframe::DoUpdateViewMenu(void)
{
    RS_INFO->Lock();

    // make sure that there is a valid histogram loaded
    map<string,hdef_t>::iterator hdef_itr = RS_INFO->histdefs.find(RS_INFO->current.hnamepath);
    if( (RS_INFO->current.hnamepath == "")
	|| (hdef_itr == RS_INFO->histdefs.end()) ) {
	
	// clear menu settings
	fMenuView->UnCheckEntry(M_VIEW_LOGX);
	fMenuView->UnCheckEntry(M_VIEW_LOGY);

	RS_INFO->Unlock();
	return;
    }

    // set menu items
    if(hdef_itr->second.display_info.use_logx)
	fMenuView->CheckEntry(M_VIEW_LOGX);
    else
	fMenuView->UnCheckEntry(M_VIEW_LOGX);
    if(hdef_itr->second.display_info.use_logy)
	fMenuView->CheckEntry(M_VIEW_LOGY);
    else
	fMenuView->UnCheckEntry(M_VIEW_LOGY);
    
	
    RS_INFO->Unlock();

}

//-------------------
// DoLoadHistsList
//-------------------
void rs_mainframe::DoLoadHistsList(void)
{

  // list of histo / server combinations
  //vector<hid_t> hids;
  vector<string> new_hnamepaths;  // the histograms to activate

  // write them to disk outside of the mutex
  TGFileInfo* fileinfo = new TGFileInfo();
  new TGFileDialog(gClient->GetRoot(), gClient->GetRoot(), kFDOpen, fileinfo);

  ///////// CHECK FOR ERRORS ///////////////
  
  ifstream ifs(fileinfo->fFilename);
  if(!ifs.is_open()){
    cout<<"Unable to read file \""<<fileinfo->fFilename<<"\"!"<<endl;
    return;
  }

  int viewStyle;
  ifs >> viewStyle;
  if(viewStyle == 0 )
    RS_INFO->viewStyle = rs_info::kViewByObject;
  else  if(viewStyle == 1 )
    RS_INFO->viewStyle = rs_info::kViewByServer;
  else 
    RS_INFO->viewStyle = rs_info::kViewByObject;

  _DBG_ << RS_INFO->viewStyle << endl;

  // make sure we are reading in OK...
  while(!ifs.eof()) {
    string in_hist;
    ifs >> in_hist;

    if( in_hist == "" )
      break;

    _DBG_ << in_hist << endl;
    new_hnamepaths.push_back( in_hist );
  }

  ifs.close();
  cout<<"Histogram list read from \""<<fileinfo->fFilename<<"\""<<endl;

  RS_INFO->Lock();

  
  // loop through all current servers and histograms, and set all of them to not display
  for(map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.begin();
      hdef_iter != RS_INFO->histdefs.end(); hdef_iter++) {
	  hdef_iter->second.active = false; 
	  for(map<string, bool>::iterator hdefserver_iter = hdef_iter->second.servers.begin();
	      hdefserver_iter != hdef_iter->second.servers.end(); hdefserver_iter++) {
		  hdefserver_iter->second = false;
	  }
  }
  for(map<string,server_info_t>::iterator server_iter = RS_INFO->servers.begin();
      server_iter != RS_INFO->servers.end(); server_iter++) {         
	  server_iter->second.active = false;
  }
  
  // server names are usually ephemeral, so we only load histogram path/name combos
  // we enable the histos for all servers, but have to do this differently
  // depending on the view model
  for( vector<string>::iterator hnamepath_iter = new_hnamepaths.begin();
       hnamepath_iter != new_hnamepaths.end(); hnamepath_iter++ ) {

    map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(*hnamepath_iter);
    if(hdef_iter==RS_INFO->histdefs.end()) continue;

    // set all servers to load this histogram
    for(map<string, bool>::iterator hdefserver_iter = hdef_iter->second.servers.begin();
	hdefserver_iter != hdef_iter->second.servers.end(); hdefserver_iter++) {
      hdefserver_iter->second = true;

      if(RS_INFO->viewStyle == rs_info::kViewByServer) {
	map<string,server_info_t>::iterator server_info_iter = RS_INFO->servers.find(hdefserver_iter->first);
	if(server_info_iter==RS_INFO->servers.end()) continue;
	server_info_iter->second.active = true;
      }
    }
  }
  

  // is setting current working right?
  
  // For production use, don't displaying anything by default when starting out
/*
  // If the RS_INFO->current value is not set, then set it to the first server/histo
  // and set the flag to have DoUpdate called
  if(RS_INFO->servers.find(RS_INFO->current.serverName)==RS_INFO->servers.end()){
    if(RS_INFO->servers.size()>0){
      map<string,server_info_t>::iterator server_iter = RS_INFO->servers.begin();
      if(server_iter->second.hnamepaths.size()>0){
	RS_INFO->current = hid_t(server_iter->first, server_iter->second.hnamepaths[0]);
      }
    }
  }

  RS_INFO->update = true;
*/
  RS_INFO->Unlock();

}

//-------------------
// DoSaveHistsList
//-------------------
void rs_mainframe::DoSaveHistsList(void)
{
  // get list of histo / server combinations
  vector<string> hists_tosave;

  RS_INFO->Lock();
  
  // keep track of what histograms are selected 
  // if a histogram is listed as active on any server, then save it
  for(map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.begin(); 
      hdef_iter != RS_INFO->histdefs.end(); hdef_iter++) {

    // check to see if it's available on any servers - should only matter for view-by-server
    bool active_on_servers = false;
    if(RS_INFO->viewStyle == rs_info::kViewByServer) {
      for(map<string, bool>::iterator hdefserver_iter = hdef_iter->second.servers.begin();
	  hdefserver_iter != hdef_iter->second.servers.end(); hdefserver_iter++) {    
	if(hdefserver_iter->second) {
	  active_on_servers = true;
	  break;
	}
      }
    }

    if( hdef_iter->second.active || active_on_servers )
      hists_tosave.push_back( hdef_iter->second.hnamepath );
  }

  RS_INFO->Unlock();
  
  // write them to disk outside of the mutex
  TGFileInfo* fileinfo = new TGFileInfo();
  new TGFileDialog(gClient->GetRoot(), gClient->GetRoot(), kFDSave, fileinfo);

  ///////// CHECK FOR ERRORS ///////////////
  
  ofstream ofs(fileinfo->fFilename);
  if(!ofs.is_open()){
    cout<<"Unable to create file \""<<fileinfo->fFilename<<"\"!"<<endl;
    return;
  }

  ofs << RS_INFO->viewStyle << endl;
  for(vector<string>::const_iterator hit = hists_tosave.begin();
      hit != hists_tosave.end(); hit++) {
    ofs << *hit << endl;
  }

  /*
  for(vector<hid_t>::const_iterator hit = hids.begin();
      hit != hids.end(); hit++) {
    ofs << *hit << endl;
  }
  */

  ofs.close();
  cout<<"Histogram list written to \""<<fileinfo->fFilename<<"\""<<endl;
}

//-------------------
// DoFinal
//-------------------
void rs_mainframe::DoFinal(void) {
	//loop servers
	map<string, server_info_t>::iterator serviter = RS_INFO->servers.begin();
	for(; serviter != RS_INFO->servers.end(); serviter++) {
		string server = serviter->first;
		vector<string> paths = serviter->second.hnamepaths;		
		if( RS_CMSG ) RS_CMSG->FinalHistogram(server, paths);
		if( RS_XMSG ) RS_XMSG->FinalHistogram(server, paths);
	}
}

//-------------------
// DoELog
//-------------------
void rs_mainframe::DoELog(void)
{
	// Originally, this was handled in this program. It has
	// since been broken off into the RSelog command line
	// program. Now we just need to execute that. Because the
	// program may take a bit to gather hists and make plots,
	// we launch a separate thread to handle it.
	
	thread thr( &rs_mainframe::ELogEntryThread, this );
	
	thr.detach();

}

//-------------------
// ELogEntryThread
//-------------------
void rs_mainframe::ELogEntryThread(void)
{
	// This thread forms the RSelog command for making
	// the e-log entry and then executes it. It is done
	// in a separate thread so the GUI is not held up
	// waiting for this to finish.


#ifdef HAVE_EZCA
	ezcaSetTimeout(0.2);   // default in ezca lib is 0.05
	ezcaSetRetryCount(50); // default in ezca lib is 599
	string epics_var_name = "HD:coda:daq:run_number";
	int err = ezcaGet((char*)(epics_var_name.c_str()), ezcaLong, 1, &epics_run_number);
        //	if(err != EZCA_OK) epics_run_number = 0;
#endif // HAVE_EZCA

	// Make list of all hnamepaths for all tabs
	vector<string> hnamepaths;
	for(auto t : rstabs) {
		for( auto h : t->hnamepaths ) hnamepaths.push_back(h);
	}

	// Build command
	stringstream cmd;
	cmd << "hdlog -c RSelog -L " << ELOG_NAME;
	if(ELOG_NOTIFY) cmd << " -e " << ELOG_EMAIL;
	if(epics_run_number>0) cmd << " -R " << epics_run_number;
	for( string s : hnamepaths ) cmd << " -H \"" << s << "\"";
	
	// Execute command
	system(cmd.str().c_str());
}

//-------------------
// DoHDMonGUI
//-------------------
void rs_mainframe::DoHDMonGUI(void)
{
	// Here, we want to run the hdmongui.py program as a
	// separate, detached process. To do this, we must fork
	// this process and replace the child with the new process.
	pid_t child_pid = fork();
	if( child_pid == 0 ) {
		execlp("hdmongui.py", "hdmongui.py", NULL);
	}
}

//-------------------
// DoReferencePlot
//-------------------
void rs_mainframe::DoReferencePlot(void)
{
	if(!dialog_referenceplot){
		dialog_referenceplot = new Dialog_ReferencePlot(gClient->GetRoot(), 10, 10);
	}else{
		dialog_referenceplot->MapWindow();
		dialog_referenceplot->RaiseWindow();
		dialog_referenceplot->RequestFocus();
	}
}

//-------------------
// DoMakeReferencePlot
//-------------------
void rs_mainframe::DoMakeReferencePlot(void)
{
	/// Save current canvas as a .png file in the appropriate directory
	/// so that it may be shown as the reference plot when displaying
	/// this hnamepath. Prior to making this the new reference, a dialog
	/// is presented to the user so they may authenticate themselves
	/// and confirm that they really want to do this. The heavy lifting
	/// for all of this is done in the Dialog_NewReference class.
	
	// We make the temporary reference plot here first since it's possible
	// the user has "auto-advance" turned on and the canvas will change soon.
	// This will at least not make use wait for instantiation of the
	// Dialog_NewReference so perhaps we win the race slightly more often.
	string &hnamepath = current_tab->currently_displayed_hnamepath;
	string tmp_hnamepath = string("tmp") + hnamepath;
	string tmp_fname = Dialog_ReferencePlot::MakeReferenceFilename(tmp_hnamepath);
	current_tab->canvas->SaveAs(tmp_fname.c_str(), "");
	
	// Create the new dialog. It will handle moving the files as necesary
	// upon destruction. 
	dialog_newreference = new Dialog_NewReference(gClient->GetRoot(), 1000, 800, hnamepath, tmp_fname);
	

//	// The password dialog caused an immediate crash while testing on
//	// Linux. Disable it for now.
//	char prompt[512];
//	char pwdbuff[256] = "password";
//	sprintf(prompt, "Please enter the password for setting the reference plot for \"%s\"", current_tab->currently_displayed_hnamepath.c_str());
//	RSPasswdDialog d(prompt, pwdbuff, 256);
//	
//	if( string(pwdbuff) != "password" ){
//		cout << "Invalid reference plot password." << endl;
//		return;
//	}
//
//	string fname = Dialog_ReferencePlot::MakeReferenceFilename(current_tab->currently_displayed_hnamepath);
//
//	// If a reference plot already exists, then move it into the archive directory
//	// with the current time prefixing the name.
//	bool file_exists = (access(fname.c_str(), F_OK) != -1);
//	if(file_exists){
//	
//		// Get archive directory name and make sure it exists
//		string dirname = Dialog_ReferencePlot::GetReferenceArchiveDir();
//		mkdir(dirname.c_str(), 0777); // make directory if it doesn't exist
//
//		// Generate filename based on current time
//		time_t now = time(NULL);
//		auto nowlt = localtime(&now);
//		char tmbuf[256];
//		strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d_%H:%M:%S", nowlt);
//		string time_prefix(tmbuf);
//		
//		// Strip path off of image filename
//		size_t pos = fname.find_last_of('/');
//		if(pos != string::npos){
//			string archivename = dirname + "/" + time_prefix + fname.substr(pos+1);
//			cout << "Archiving old reference:" << endl;
//			cout << "    " << fname << " -> " << archivename << endl;
//			rename(fname.c_str(), archivename.c_str());
//		}
//	}
//
//	current_tab->canvas->SaveAs(fname.c_str(), "");
}

//-------------------
// AddLabel
//-------------------
TGLabel* rs_mainframe::AddLabel(TGCompositeFrame* frame, string text, Int_t mode, ULong_t hints)
{
	TGLabel *lab = new TGLabel(frame, text.c_str());
	lab->SetTextJustify(mode);
	lab->SetMargins(0,0,0,0);
	lab->SetWrapLength(-1);
	frame->AddFrame(lab, new TGLayoutHints(hints,2,2,2,2));

	return lab;
}

//-------------------
// AddButton
//-------------------
TGTextButton* rs_mainframe::AddButton(TGCompositeFrame* frame, string text, ULong_t hints)
{
	TGTextButton *b = new TGTextButton(frame, text.c_str());
	b->SetTextJustify(36);
	b->SetMargins(0,0,0,0);
	b->SetWrapLength(-1);
	b->Resize(100,22);
	frame->AddFrame(b, new TGLayoutHints(hints,2,2,2,2));

	return b;
}

//-------------------
// AddCheckButton
//-------------------
TGCheckButton* rs_mainframe::AddCheckButton(TGCompositeFrame* frame, string text, ULong_t hints)
{
	TGCheckButton *b = new TGCheckButton(frame, text.c_str());
	b->SetTextJustify(36);
	b->SetMargins(0,0,0,0);
	b->SetWrapLength(-1);
	frame->AddFrame(b, new TGLayoutHints(hints,2,2,2,2));

	return b;
}

//-------------------
// AddPictureButton
//-------------------
TGPictureButton* rs_mainframe::AddPictureButton(TGCompositeFrame* frame, string picture, string tooltip, ULong_t hints)
{
	TGPictureButton *b = new TGPictureButton(frame, gClient->GetPicture(picture.c_str()));
	if(tooltip.length()>0) b->SetToolTipText(tooltip.c_str());
	frame->AddFrame(b, new TGLayoutHints(hints,2,2,2,2));

	return b;
}

//-------------------
// AddSpacer
//-------------------
TGFrame* rs_mainframe::AddSpacer(TGCompositeFrame* frame, UInt_t w, UInt_t h, ULong_t hints)
{
	/// Add some empty space. Usually, you'll only want to set w or h to
	/// reserve width or height pixels and set the other to "1".

	TGFrame *f =  new TGFrame(frame, w, h);
	frame->AddFrame(f, new TGLayoutHints(hints ,2,2,2,2));
	
	return f;
}

//-------------------
// CreateGUI
//-------------------
void rs_mainframe::CreateGUI(void)
{
	// Use the "color wheel" rather than the classic palette.
	TColor::CreateColorWheel();
	
   //==============================================================================================
   // make a menubar
   // Create menubar and popup menus. The hint objects are used to place
   // and group the different menu widgets with respect to eachother.

   fMenuBarLayout = new TGLayoutHints(kLHintsTop | kLHintsExpandX);
   fMenuBarItemLayout = new TGLayoutHints(kLHintsTop | kLHintsLeft, 0, 4, 0, 0);
   
   fMenuFile = new TGPopupMenu(gClient->GetRoot());
   fMenuFile->AddEntry("&Open List...", M_FILE_OPEN);
   fMenuFile->AddEntry("&Save List...", M_FILE_SAVE);
   fMenuFile->AddSeparator();
   fMenuFile->AddEntry("New Configuration...", M_FILE_NEW_CONFIG);
   fMenuFile->AddEntry("Open Configuration...", M_FILE_OPEN_CONFIG);
   fMenuFile->AddEntry("Save Configuration", M_FILE_SAVE_CONFIG);
   fMenuFile->AddEntry("Save Configuration As ...", M_FILE_SAVE_AS_CONFIG);
   fMenuFile->AddEntry("E&xit", M_FILE_EXIT);

   fMenuTools = new TGPopupMenu(gClient->GetRoot());
   fMenuTools->AddEntry("Config Macros...", M_TOOLS_MACROS);
   fMenuTools->AddEntry("Start TBrowser", M_TOOLS_TBROWSER);
   fMenuTools->AddEntry("View Tree Info", M_TOOLS_TREEINFO);
   fMenuTools->AddEntry("Save Hists...",  M_TOOLS_SAVEHISTS);
   fMenuTools->AddSeparator();
   fMenuTools->AddEntry("Reset Histograms/Macros...",  M_TOOLS_RESET);
   
   fMenuView = new TGPopupMenu(gClient->GetRoot());
   fMenuView->AddEntry("New Tab...", M_VIEW_NEW_TAB);
   fMenuView->AddEntry("Remove Tab...", M_VIEW_REMOVE_TAB);
   fMenuView->AddSeparator();
   fMenuView->AddEntry("Log X axis", M_VIEW_LOGX);
   fMenuView->AddEntry("Log Y axis", M_VIEW_LOGY);
   fMenuView->AddEntry("Scale Options...", M_VIEW_SCALE_OPTS);

   fMenuBar = new TGMenuBar(this, 1, 1, kHorizontalFrame | kRaisedFrame );
   this->AddFrame(fMenuBar, fMenuBarLayout);
   fMenuBar->AddPopup("&File",  fMenuFile, fMenuBarItemLayout);
   fMenuBar->AddPopup("&Tools", fMenuTools, fMenuBarItemLayout);
   fMenuBar->AddPopup("&View",  fMenuView, fMenuBarItemLayout);

   // connect the menus to methods
   // Menu button messages are handled by the main frame (i.e. "this")
   // HandleMenu() method.
   fMenuFile->Connect("Activated(Int_t)", "rs_mainframe", this, "HandleMenu(Int_t)");
   fMenuTools->Connect("Activated(Int_t)", "rs_mainframe", this, "HandleMenu(Int_t)");
   fMenuView->Connect("Activated(Int_t)", "rs_mainframe", this, "HandleMenu(Int_t)");
   
   
	//==============================================================================================
	// Fill in main content of window. Note that most of the area is taken up by the
	// contents of the TGTab object which is filled in by the RSTab class constructor.

	// Main vertical frame
	TGVerticalFrame *fMain = new TGVerticalFrame(this);
	this->AddFrame(fMain, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY,2,2,2,2));

	// Top, middle(tabs), and bottom frames
	TGHorizontalFrame *fMainTop = new TGHorizontalFrame(fMain);
	                   fMainTab = new TGTab(fMain);
	TGHorizontalFrame *fMainBot = new TGHorizontalFrame(fMain);

	fMain->AddFrame(fMainTop, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));
	fMain->AddFrame(fMainTab, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX | kLHintsExpandY,2,2,2,2));
	fMain->AddFrame(fMainBot, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));

	//....... Top Frame .......
	TGVerticalFrame *fMainTopLeft  = new TGVerticalFrame(fMainTop);
	TGGroupFrame    *fMainTopRight = new TGGroupFrame(fMainTop,"Update Options",kHorizontalFrame | kRaisedFrame);
	TGVerticalFrame *fMainTopRightOptions  = new TGVerticalFrame(fMainTopRight);

	fMainTop->AddFrame(fMainTopLeft , new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));
	fMainTop->AddFrame(fMainTopRight, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));
	fMainTopRight->AddFrame(fMainTopRightOptions, new TGLayoutHints(kLHintsLeft | kLHintsTop ,2,2,2,2));

	// UDL Label
	string udl_label = "UDL = "+ROOTSPY_UDL + string(20, ' ');
	lUDL = AddLabel(fMainTopLeft, udl_label);

	// Buttons
	TGHorizontalFrame *fMainTopLeftButtons = new TGHorizontalFrame(fMainTopLeft);
	fMainTopLeft->AddFrame(fMainTopLeftButtons, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

	TGTextButton *bSetArchive = AddButton(fMainTopLeftButtons, "Set Archive   ");
	bShowOverlays = AddCheckButton(fMainTopLeftButtons, "Show Archived Hists       ");

	// label for archive file name display
	lArchiveFile = AddLabel(fMainTopLeft, "No Archive Loaded", kTextLeft, kLHintsLeft | kLHintsTop | kLHintsExpandX);

	// Update options
	bAutoRefresh = AddCheckButton(fMainTopRightOptions, "Auto-refresh     ");
	bAutoAdvance = AddCheckButton(fMainTopRightOptions, "Auto-advance     ");
	bAutoRefresh->SetState(kButtonDown);

   // Delay
   TGHorizontalFrame *fMainTopRightDelay = new TGHorizontalFrame(fMainTopRight);
   AddLabel(fMainTopRightDelay, "delay:    ");
   fDelay = new TGComboBox(fMainTopRightDelay,"-",-1,kHorizontalFrame | kSunkenFrame | kDoubleBorder | kOwnBackground);
   fDelay->AddEntry("0s",0);
   fDelay->AddEntry("1s",1);
   fDelay->AddEntry("2s",2);
   fDelay->AddEntry("3s",3);
   fDelay->AddEntry("4s",4);
   fDelay->AddEntry("10s ",10);
   fDelay->Resize(50,22);
   fDelay->Select(delay_time = 4);
   fDelay->GetTextEntry()->SetText("4s");
   fMainTopRightDelay->AddFrame(fDelay, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   fMainTopRight->AddFrame(fMainTopRightDelay, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
	
	//....... Tabs .......
	// Add a single tab for now
	//new RSTab(this, "New");

	//....... Bottom Frame .......
	AddSpacer(fMainBot, 50, 1, kLHintsRight);
	TGTextButton *bElog = AddButton(fMainBot, "Make e-log Entry        ", kLHintsLeft);
	TGTextButton *bRefs = AddButton(fMainBot, "Show Reference Plot       ", kLHintsLeft);
	AddSpacer(fMainBot, 50, 1, kLHintsLeft);
	TGTextButton *bMakeRef = AddButton(fMainBot, "Make this new Reference Plot           ", kLHintsLeft);
	AddSpacer(fMainBot, 50, 1, kLHintsLeft);
	TGTextButton *bDropHists = AddButton(fMainBot, "Drop Hists          ", kLHintsLeft);
	AddSpacer(fMainBot, 50, 1, kLHintsLeft);
	TGTextButton *bHDMonGUI = AddButton(fMainBot, "Start hdmongui        ", kLHintsLeft);
	TGTextButton *bQuit = AddButton(fMainBot, "Quit  ", kLHintsRight);

	fMain->MapSubwindows();
	fMain->MapWindow();

	//==================== Connect GUI elements to methods ====================
	bQuit->Connect("Clicked()","rs_mainframe", this, "DoQuit()");
	bHDMonGUI->Connect("Clicked()","rs_mainframe", this, "DoHDMonGUI()");
	bElog->Connect("Clicked()","rs_mainframe", this, "DoELog()");
	bRefs->Connect("Clicked()","rs_mainframe", this, "DoReferencePlot()");
	bMakeRef->Connect("Clicked()","rs_mainframe", this, "DoMakeReferencePlot()");
	bDropHists->Connect("Clicked()","rs_mainframe", this, "DropAllHists()");
	fMainTab->Connect("Selected(Int_t)", "rs_mainframe", this, "DoTabSelected(Int_t)");
	bSetArchive->Connect("Clicked()","rs_mainframe", this, "DoSetArchiveFile()");
	fDelay->Connect("Selected(Int_t)","rs_mainframe", this, "DoSelectDelay(Int_t)");

}

//// wrapper for histogram drawing
//void rs_mainframe::DrawHist(TCanvas *the_canvas, TH1 *hist, string hnamepath,
//			    hdef_t::histdimension_t hdim,
//			    hdisplay_info_t &display_info)
//{
//    string histdraw_args = "";
//    bool overlay_enabled = (show_archived_hists->GetState()==kButtonDown);
//    //bool overlay_enabled = false;
//    //double hist_line_width = 1.;
//    float overlay_ymin=0., overlay_ymax=0.;
//    
//    //the_canvas->Divide(1);
//    the_canvas->cd(0);
//
//    if(display_info.use_logx)
//	the_canvas->SetLogx();
//    else
//	the_canvas->SetLogx(0);
//    if(display_info.use_logy)
//	the_canvas->SetLogy();
//    else
//	the_canvas->SetLogy(0);
//
//    if(hdim == hdef_t::oneD) {
//	
//      //if(hdim == hdef_t::histdimension_t::oneD) {
//	bool do_overlay = false;
//	//TH1 *archived_hist = NULL;
//
//	// check for archived histograms and load them if they exist and we are overlaying
//	// we do this first to determine the parameters needed to overlay the histograms
//	if(overlay_enabled && (archive_file!=NULL) ) {
//	    _DBG_<<"trying to get archived histogram: " << hnamepath << endl;
//	    TH1* archived_hist = (TH1*)archive_file->Get(hnamepath.c_str());
//	    
//	    if(archived_hist) { 
//		// only plot the archived histogram if we can find it
//		_DBG_<<"found it!"<<endl;
//		do_overlay = true;
//	
//		// only display a copy of the archived histogram so that we can 
//		// compare to the original
//		if(overlay_hist)
//		    delete overlay_hist;
//		overlay_hist = (TH1*)(archived_hist->Clone());   // memory leak?
//
//		overlay_hist->SetStats(0);  // we want to compare just the shape of the archived histogram
//		overlay_hist->SetFillColor(5); // draw archived histograms with shading
//
//		overlay_ymin = overlay_hist->GetMinimum();
//		overlay_ymax = 1.1*overlay_hist->GetMaximum();
//
//		gStyle->SetStatX(0.85);
//	    } else {
//		gStyle->SetStatX(0.95);
//	    }
//	    
//	    // update histogram with current style parameters
//	    // primarily used to update statistics box
//	    hist->UseCurrentStyle();
//	}
//	
//	
//	// draw summed histogram
//	if(!do_overlay) {
//	    hist->Draw();
//
//	} else {
//	    // set axis ranges so that we can sensibly overlay the histograms
//	    double hist_ymax = 1.1*hist->GetMaximum();
//	    //double hist_ymax = hist->GetMaximum();
//	    double hist_ymin = hist->GetMinimum();
//	    // make sure we at least go down to zero, so that we are displaying the whole
//	    // distribution - assumes we are just looking at frequency histograms
//	    if(hist_ymin > 0)
//		hist_ymin = 0;
//	    // add in a fix for logarithmic y axes
//	    if(display_info.use_logy && hist_ymin == 0)
//		hist_ymin = 0.1;
//	    
//	    hist->GetYaxis()->SetRangeUser(hist_ymin, hist_ymax); 
//
//	    // draw the current histogram with points/error bars if overlaying
//	    hist->SetMarkerStyle(20);
//	    hist->SetMarkerSize(0.7);
//	    
//	    // scale down archived histogram and display it to set the scale
//	    // appropriately scale the overlay histogram
//	    //overlay_hist->Scale(scale);
//	    if(display_info.overlay_scale_mode == 1) {   
//		// scale both to same overall peak value
//		float scale = hist_ymax/overlay_ymax;
//		overlay_hist->Scale(scale);
//	    } else if(display_info.overlay_scale_mode == 2) {
//		// scale to same integral over specified bin range (lo bin # - hi bin #)
//		float scale = hist->Integral(display_info.scale_range_low, display_info.scale_range_hi)
//		    / overlay_hist->Integral(display_info.scale_range_low, display_info.scale_range_hi);
//		overlay_hist->Scale(scale);
//	    } else if(display_info.overlay_scale_mode == 3) {
//		// scale to same integral over specified bin range (lo % of range - hi % of range)
//		int lo_bin = static_cast<int>( (double)(hist->GetNbinsX()) * display_info.scale_range_low/100. );
//		int hi_bin = static_cast<int>( (double)(hist->GetNbinsX()) * display_info.scale_range_hi/100. );
//		float scale = hist->Integral(lo_bin, hi_bin) / overlay_hist->Integral(lo_bin, hi_bin);
//		overlay_hist->Scale(scale);
//	    }
//
//	    overlay_hist->Draw();
//
//	    // now print the current histogram on top of it
//	    hist->Draw("SAME E1");
//
//	    // add axis to the right for scale of archived histogram
//	    if(overlay_yaxis != NULL)
//		delete overlay_yaxis;
//	    //overlay_yaxis = new TGaxis(gPad->GetUxmax(),hist_ymin,
//				       //gPad->GetUxmax(),hist_ymax,
//	    overlay_yaxis = new TGaxis(gPad->GetUxmax(),gPad->GetUymin(),
//				       gPad->GetUxmax(),gPad->GetUymax(),
//				       overlay_ymin,overlay_ymax,510,"+L");
//	    overlay_yaxis->SetLabelFont( hist->GetLabelFont() );
//	    overlay_yaxis->SetLabelSize( hist->GetLabelSize() );
//	    overlay_yaxis->Draw();
//	}
//
//    } else if(hdim == hdef_t::twoD) {
//
//	bool do_overlay = false;
//	TH1* archived_hist = NULL;
//	the_canvas->Clear();
//	the_canvas->cd(0);
//	if(overlay_enabled && (archive_file!=NULL) ) {
//	    _DBG_<<"trying to get archived histogram: " << hnamepath << endl;
//	    archived_hist = (TH1*)archive_file->Get(hnamepath.c_str());
//	    
//	    if(archived_hist) { 
//		do_overlay = true;
//		the_canvas->Divide(2);
//		the_canvas->cd(2);
//		archived_hist->Draw("COLZ");
//		the_canvas->cd(1);
//	    }
//	}
//
//	// handle drawing 2D histograms differently
//	hist->Draw("COLZ");   // do some sort of pretty printing
//	
//    } else {
//	// default to drawing without arguments
//	hist->Draw();
//    }
//
//
//    // finally, after we've finished drawing on the screen, run any macros that may exist
//    map<string,string>::iterator macro_iter = macro_files.find(hnamepath);
//    if(macro_iter != macro_files.end()) {
//      stringstream ss;
//      ss << ".x " << macro_iter->second;
//      
//      _DBG_ << "running macro = " << macro_iter->second << endl;
//      
//      exec_shell->Exec(ss.str().c_str());
//    }
//}

//-------------------
// GetAllDisplayedHNamepaths
//-------------------
void rs_mainframe::GetAllDisplayedHNamepaths(set<string> &hnamepaths)
{
	// Fill the provided container with list of all hnamepaths
	// of all tabs. Additionally, if any hnamepaths are macros
	// then add any hnamepaths they depend on to the list.
	// This is called from RSTab::DoReset so that all histograms
	// can be reset at once.
	for(auto rstab : rstabs){
		for(string hnamepath : rstab->hnamepaths) hnamepaths.insert(hnamepath);
	}
	
	RS_INFO->Lock();
	for(string hnamepath : hnamepaths){
		auto it = RS_INFO->histdefs.find(hnamepath);
		if( it == RS_INFO->histdefs.end() ) continue;
		for(string s : it->second.macro_hnamepaths){
			hnamepaths.insert(s);
		}
	}
	RS_INFO->Unlock();
}

//-------------------
// DrawMacro
//-------------------
void rs_mainframe::DrawMacro(TCanvas *the_canvas, hinfo_t &the_hinfo)
{
	// we're given an hinfo_t which corresponds to just one server
	// so we can go ahead and execute the script we are given

	if(!the_hinfo.macroData) {
		_DBG_ << "Trying to draw a macro with no saved data!" << endl;
		return;
	}

	// move to the right canvas and draw!
	the_canvas->cd();
	if(the_hinfo.Nkeys == 1){
		ExecuteMacro(RS_INFO->sum_dir, the_hinfo.macroString);
	}else{
		ExecuteMacro(the_hinfo.macroData, the_hinfo.macroString);
	}
	the_canvas->Update();
}

//-------------------
// DrawMacro
//-------------------
void rs_mainframe::DrawMacro(TCanvas *the_canvas, hdef_t &the_hdef)
{
	//const string TMP_FILENAME = ".summed_file.root";

	// We have a few different possible scenarios here
	// The cases where there are zero or one attached servers are straightforward
	// If there are multiple attached servers, we need to merge the data somehow
	int num_servers = the_hdef.hists.size();
	//cerr << " number of servers = " << num_servers << endl;
	if( num_servers == 0 ) {
		_DBG_ << "Trying to draw a macro with data from no servers!" << endl;
		return;
	} else if( num_servers == 1 ) {
		DrawMacro(the_canvas, the_hdef.hists.begin()->second);
	} else {
		// we could have different versions of these scripts, pick the latest
		int best_version = 0;
		const hinfo_t *hinfo_best_version = NULL;
		for(map<string, hinfo_t>::const_iterator hinfo_itr = the_hdef.hists.begin();
		    hinfo_itr != the_hdef.hists.end(); hinfo_itr++) {
			if( hinfo_itr->second.macroVersion >= best_version ){
				hinfo_best_version = &hinfo_itr->second;
				best_version = hinfo_best_version->macroVersion;
			}
		}
		
		// If the TMemfile only had 1 key, then we assume it was just a macro string
		// and that it will be plotting histograms from the RS_INFO->sum_dir.
		if(hinfo_best_version->Nkeys == 1){
		
			DrawMacro(the_canvas, the_hdef.hists.begin()->second);

		}else{
		
			// Histograms came with TMemFile. Merge them before plotting
		
			// now combine the data
			TMemFile *first_file = NULL;
			//vector<TFile *> file_list;
			//TList file_list;
			TList *file_list = new TList();
			//file_list->SetOwner(kFALSE); // this doesn't work??

			//cerr << "BUILD FILE LIST" << endl;
			for(map<string, hinfo_t>::const_iterator hinfo_itr = the_hdef.hists.begin();
				hinfo_itr != the_hdef.hists.end(); hinfo_itr++) {
				//cerr << hinfo_itr->second.macroData->GetName() << endl;
				//hinfo_itr->second.macroData->ls();
				if( hinfo_itr->second.macroVersion == best_version ) {
					if(first_file == NULL)   // save the first file so that we can get unique data out of it
						first_file = hinfo_itr->second.macroData;
					//file_list.push_back( hinfo_itr->second.macroData );
					hinfo_itr->second.macroData->SetBit(kCanDelete, kFALSE);
					file_list->Add( hinfo_itr->second.macroData );
				}
			}

			//cerr << "MERGE FILES" << endl;
			//file_list->SetOwner(kFALSE);
			static TMemFile *macro_data = new TMemFile(".rootspy_macro_tmp.root", "RECREATE");
			macro_data->Delete("T*;*");
			MergeMacroFiles(macro_data, file_list);

			//cerr << "======= MERGED FILES ==========" << endl;
			//macro_data->ls();

			// extract the macro
			TObjString *macro_str = (TObjString *)first_file->Get("macro");
			if(!macro_str) {
				_DBG_ << "Could not extract macro from " << first_file->GetName() << endl;
				return;
			}
			string the_macro( macro_str->GetString().Data() );

			// move to the right canvas and draw!
			the_canvas->cd();
			ExecuteMacro(macro_data, the_macro);
			the_canvas->Update();
			//f->Close();
			//unlink( TMP_FILENAME.c_str() );
			//file_list->Clear();
			delete file_list;
		}
	}
}

//-------------------
// ExecuteMacro
//-------------------
void rs_mainframe::ExecuteMacro(TDirectory *f, string macro)
{
	// Lock ROOT
	pthread_rwlock_wrlock(ROOT_MUTEX);
	
	// Keep a separate TSyle for each macro we draw. This used to
	// allow macros to change the style and have it stay changed 
	// until the next macro is drawn.
	TStyle savestyle(*gStyle);
	static std::map<string, TStyle*> styles;
	if( styles.count( macro ) == 0 ){
		// n.b. 12/19/2019 DL  -  The following originally passed *gStyle into the TStyle
		// constructor. It was this way for a long time, but it was recently noticed that
		// a side effect was to inherit some style settings from a previous macro. I'm
		// adding this comment just because it had been that way for so long, if this change
		// has an unanticipated affect I can remind myself of the the history.
		styles[macro] = new TStyle();  // originally TStyle(gStyle)
	}
	*gStyle = *styles[macro];

	// Reset color palette to default. Macros may change this
	// with gStyle->SetPalette(...) but it is not actually a
	// property of the TSyle object. The SetPalette method just
	// maps to a static method of TColor which changes the palette
	// for the current TCanvas. (At least that is how I think this
	// works!)
	gStyle->SetPalette(); // reset to default 	

	TDirectory *savedir = gDirectory;
	f->cd();

	// execute script line-by-line
	// maybe we should do some sort of sanity check first?
	istringstream macro_stream(macro);
	int iline = 0;
	while(!macro_stream.eof()) {
		string s;
		getline(macro_stream, s);
		// _DBG_ << s << "\n";
		iline++;
		
		// Special comment lines allow macro to communicate to RootSpy system
		string prefix = "// hnamepath:";
		if(s.find(prefix) == 0){
			uint32_t spos = prefix.length();
			while( spos<s.length() && (s[spos]==' ' || s[spos]=='\t') ) spos++;
			uint32_t epos = s.length()-1;
			while(epos>spos && (s[epos]==' ' || s[epos]=='\t') ) epos--;

			string h = s.substr(spos, epos-spos+1); // chop off prefix + whitespace
			RS_INFO->RequestHistograms(h, false);
		}
		
		prefix = "// tree:";
		if(s.find(prefix) == 0){
			uint32_t spos = prefix.length();
			while( spos<s.length() && (s[spos]==' ' || s[spos]=='\t') ) spos++;
			uint32_t epos = s.length()-1;
			while(epos>spos && (s[epos]==' ' || s[epos]=='\t') ) epos--;

			string h = s.substr(spos, epos-spos+1); // chop off prefix + whitespace
			RS_INFO->RequestTrees(h, -1, false);
		}
		
		Long_t err = gROOT->ProcessLine(s.c_str());
		if(err == TInterpreter::kProcessing){
			cout << "Processing macro ..." << endl;
		}else  if(err != TInterpreter::kNoError){
			cout << "Error processing the macro line " << iline << ": " << err << endl;
			cout << "\"" << s << "\"" << endl;
			break;
		}
	}

	// The following was commented out because the run label just
	// won't display properly. If drawing to the canvas itself (not
	// a subpad) the label would be mostly obscured by the subpad
	// in the top corner. Drawing on pad 1 is not a great option since
	// the label was scaled and often cropped. For the FDC occupancy
	// screen, the asymmetic pads made the run label not show up
	// at all (??). Leaving this here in case I want to resurrect
	// it at some point in the future.
	if(!run_number_label && epics_run_number>0){
		run_number_label = new TLatex();
		run_number_label->SetTextSize(1.0);
		run_number_label->SetTextColor(kBlack);
		run_number_label->SetTextAlign(22);
	}
	
	// If a run number label exists, draw it on current canvas
	if(run_number_label){
		current_tab->canvas->cd(0);
		TPad *pad = (TPad*)gDirectory->FindObjectAny("run_number_label_pad");
		if(!pad) pad = new TPad("run_number_label_pad", "Run Number", 0.91, 0.975, 1.0, 1.0);
		pad->Draw();
		pad->cd();
		char str[256];
		sprintf(str, "Run: %d", (int)epics_run_number);
		run_number_label->DrawLatex(0.5, 0.5, str);
		pad->Update();
	}

	// restore
	*gStyle = savestyle;
	savedir->cd();

	// Unlock ROOT
	pthread_rwlock_unlock(ROOT_MUTEX);

}

//-------------------
// MergeMacroFiles
//-------------------
void rs_mainframe::DropAllHists(void)
{
	// Delete histograms from all servers as well as all
	// sum and reset hists. This is done when the run number
	// retrieved from EPICS changes to force a purge of all
	// values from the previous run. Since it is operating on
	// ROOT objects, it should be called from DoTimer() or
	// a GUI callback element.
	
	cout << "Purging all histograms ...." << endl;
	
	// Lock ROOT
	pthread_rwlock_wrlock(ROOT_MUTEX);

	RS_INFO->Lock();
	
	// Delete all histograms
	for(auto hinfo_it : RS_INFO->hinfos){
			hinfo_t &hinfo = RS_INFO->hinfos[hinfo_it.first];
			if(hinfo.hist     ) delete hinfo.hist;
			if(hinfo.macroData) delete hinfo.macroData;
			hinfo.hist      = NULL;
			hinfo.macroData = NULL;
	}
	RS_INFO->hinfos.clear();
	
	// Loop over hdefs and delete all sum and reset histos
	for(auto hdef_it : RS_INFO->histdefs){
		hdef_t &hdef = RS_INFO->histdefs[hdef_it.first];
		hdef.hists.clear(); //hist pointers were deleted above
		
		if(hdef.sum_hist  ) delete hdef.sum_hist;
		if(hdef.reset_hist) delete hdef.reset_hist;
		hdef.sum_hist   = NULL;
		hdef.reset_hist = NULL;
		hdef.sum_hist_present = false;
	}

	RS_INFO->Unlock();

	// Unlock ROOT
	pthread_rwlock_unlock(ROOT_MUTEX);
	
	cout << "Done" << endl;
}

//-------------------
// MergeMacroFiles
//-------------------
static Bool_t MergeMacroFiles(TDirectory *target, TList *sourcelist)
{
	Bool_t status = kTRUE;

	// Get the dir name
	TString path(target->GetPath());
	// coverity[unchecked_value] 'target' is from a file so GetPath always returns path starting with filename: 
	path.Remove(0, path.Last(':') + 2);

	Int_t nguess = sourcelist->GetSize()+1000;
	THashList allNames(nguess);
	allNames.SetOwner(kTRUE);

	((THashList*)target->GetList())->Rehash(nguess);
	((THashList*)target->GetListOfKeys())->Rehash(nguess);
   
	TFileMergeInfo info(target);

	TFile      *current_file;
	TDirectory *current_sourcedir;
	current_file      = (TFile*)sourcelist->First();
	current_sourcedir = current_file->GetDirectory(path);

	while (current_file || current_sourcedir) {
		// When current_sourcedir != 0 and current_file == 0 we are going over the target
		// for an incremental merge.
		if (current_sourcedir && (current_file == 0 || current_sourcedir != target)) {

			// loop over all keys in this directory
			TIter nextkey( current_sourcedir->GetListOfKeys() );
			TKey *key;
			TString oldkeyname;
         
			while ( (key = (TKey*)nextkey())) {

				// Keep only the highest cycle number for each key for mergeable objects. They are stored
				// in the (hash) list onsecutively and in decreasing order of cycles, so we can continue
				// until the name changes. We flag the case here and we act consequently later.
				Bool_t alreadyseen = (oldkeyname == key->GetName()) ? kTRUE : kFALSE;

				// Read in but do not copy directly the processIds.
				if (strcmp(key->GetClassName(),"TProcessID") == 0) { key->ReadObj(); continue;}

				// If we have already seen this object [name], we already processed
				// the whole list of files for this objects and we can just skip it
				// and any related cycles.
				if (allNames.FindObject(key->GetName())) {
					oldkeyname = key->GetName();
					continue;
				}

				TClass *cl = TClass::GetClass(key->GetClassName());
				if (!cl || !cl->InheritsFrom(TObject::Class())) {
					//Info("MergeRecursive", "cannot merge object type, name: %s title: %s",
					//   key->GetName(), key->GetTitle());
					continue;
				}

				// For mergeable objects we add the names in a local hashlist handling them
				// again (see above)
				if (cl->GetMerge() || cl->InheritsFrom(TDirectory::Class()) ||
				    (cl->InheritsFrom(TObject::Class()) &&
				     (cl->GetMethodWithPrototype("Merge", "TCollection*,TFileMergeInfo*") ||
				      cl->GetMethodWithPrototype("Merge", "TCollection*"))))
					allNames.Add(new TObjString(key->GetName()));
				
				// read object from first source file
				TObject *obj;
				obj = key->ReadObj();
				if (!obj) {
					//Info("MergeRecursive", "could not read object for key {%s, %s}",
					//   key->GetName(), key->GetTitle());
					continue;
				}
            
				Bool_t canBeMerged = kTRUE;

				if ( obj->IsA()->InheritsFrom( TDirectory::Class() ) ) {
					// it's a subdirectory
               
					target->cd();
					TDirectory *newdir;

					// For incremental or already seen we may have already a directory created
					if (alreadyseen) {
						newdir = target->GetDirectory(obj->GetName());
						if (!newdir) {
							newdir = target->mkdir( obj->GetName(), obj->GetTitle() );
						}
					} else {
						newdir = target->mkdir( obj->GetName(), obj->GetTitle() );
					}

					// newdir is now the starting point of another round of merging
					// newdir still knows its depth within the target file via
					// GetPath(), so we can still figure out where we are in the recursion


					// If this folder is a onlyListed object, merge everything inside.
					status = MergeMacroFiles(newdir, sourcelist);
					if (!status) return status;
				} else if (obj->IsA()->GetMerge()) {
               
					// Check if already treated
					if (alreadyseen) continue;
               
					TList inputs;
					//Bool_t oneGo = fHistoOneGo && obj->IsA()->InheritsFrom(R__TH1_Class);
               
					// Loop over all source files and merge same-name object
					TFile *nextsource = current_file ? (TFile*)sourcelist->After( current_file ) : (TFile*)sourcelist->First();
					if (nextsource == 0) {
						// There is only one file in the list
						ROOT::MergeFunc_t func = obj->IsA()->GetMerge();
						func(obj, &inputs, &info);
						info.fIsFirst = kFALSE;
					} else {
						do {
							// make sure we are at the correct directory level by cd'ing to path
							TDirectory *ndir = nextsource->GetDirectory(path);
							if (ndir) {
								ndir->cd();
								TKey *key2 = (TKey*)ndir->GetListOfKeys()->FindObject(key->GetName());
								if (key2) {
									TObject *hobj = key2->ReadObj();
									if (!hobj) {
										//Info("MergeRecursive", "could not read object for key {%s, %s}; skipping file %s",
										//   key->GetName(), key->GetTitle(), nextsource->GetName());
										nextsource = (TFile*)sourcelist->After(nextsource);
										continue;
									}
									// Set ownership for collections
									if (hobj->InheritsFrom(TCollection::Class())) {
										((TCollection*)hobj)->SetOwner();
									}
									hobj->ResetBit(kMustCleanup);
									inputs.Add(hobj);
									ROOT::MergeFunc_t func = obj->IsA()->GetMerge();
									Long64_t result = func(obj, &inputs, &info);
									info.fIsFirst = kFALSE;
									if (result < 0) {
										Error("MergeRecursive", "calling Merge() on '%s' with the corresponding object in '%s'",
										      obj->GetName(), nextsource->GetName());
									}
									inputs.Delete();
								}
							}
							nextsource = (TFile*)sourcelist->After( nextsource );
						} while (nextsource);
						// Merge the list, if still to be done
						if (info.fIsFirst) {
							ROOT::MergeFunc_t func = obj->IsA()->GetMerge();
							func(obj, &inputs, &info);
							info.fIsFirst = kFALSE;
							inputs.Delete();
						}
					}
				} else if (obj->InheritsFrom(TObject::Class()) &&
					   obj->IsA()->GetMethodWithPrototype("Merge", "TCollection*,TFileMergeInfo*") ) {
					// Object implements Merge(TCollection*,TFileMergeInfo*) and has a reflex dictionary ... 
               
					// Check if already treated
					if (alreadyseen) continue;
               
					TList listH;
					TString listHargs;
					listHargs.Form("(TCollection*)0x%lx,(TFileMergeInfo*)0x%lx", (ULong_t)&listH,(ULong_t)&info);
               
					// Loop over all source files and merge same-name object
					TFile *nextsource = current_file ? (TFile*)sourcelist->After( current_file ) : (TFile*)sourcelist->First();
					if (nextsource == 0) {
						// There is only one file in the list
						Int_t error = 0;
						obj->Execute("Merge", listHargs.Data(), &error);
						info.fIsFirst = kFALSE;
						if (error) {
							Error("MergeRecursive", "calling Merge() on '%s' with the corresponding object in '%s'",
							      obj->GetName(), key->GetName());
						}
					} else {
						while (nextsource) {
							// make sure we are at the correct directory level by cd'ing to path
							TDirectory *ndir = nextsource->GetDirectory(path);
							if (ndir) {
								ndir->cd();
								TKey *key2 = (TKey*)ndir->GetListOfKeys()->FindObject(key->GetName());
								if (key2) {
									TObject *hobj = key2->ReadObj();
									if (!hobj) {
										//Info("MergeRecursive", "could not read object for key {%s, %s}; skipping file %s",
										//   key->GetName(), key->GetTitle(), nextsource->GetName());
										nextsource = (TFile*)sourcelist->After(nextsource);
										continue;
									}
									// Set ownership for collections
									if (hobj->InheritsFrom(TCollection::Class())) {
										((TCollection*)hobj)->SetOwner();
									}
									hobj->ResetBit(kMustCleanup);
									listH.Add(hobj);
									Int_t error = 0;
									obj->Execute("Merge", listHargs.Data(), &error);
									info.fIsFirst = kFALSE;
									if (error) {
										Error("MergeRecursive", "calling Merge() on '%s' with the corresponding object in '%s'",
										      obj->GetName(), nextsource->GetName());
									}
									listH.Delete();
								}
							}
							nextsource = (TFile*)sourcelist->After( nextsource );
						}
						// Merge the list, if still to be done
						if (info.fIsFirst) {
							Int_t error = 0;
							obj->Execute("Merge", listHargs.Data(), &error);
							info.fIsFirst = kFALSE;
							listH.Delete();
						}
					}
				} else if (obj->InheritsFrom(TObject::Class()) &&
					   obj->IsA()->GetMethodWithPrototype("Merge", "TCollection*") ) {
					// Object implements Merge(TCollection*) and has a reflex dictionary ...
               
					// Check if already treated
					if (alreadyseen) continue;
               
					TList listH;
					TString listHargs;
					listHargs.Form("((TCollection*)0x%lx)", (ULong_t)&listH);
               
					// Loop over all source files and merge same-name object
					TFile *nextsource = current_file ? (TFile*)sourcelist->After( current_file ) : (TFile*)sourcelist->First();
					if (nextsource == 0) {
						// There is only one file in the list
						Int_t error = 0;
						obj->Execute("Merge", listHargs.Data(), &error);
						if (error) {
							Error("MergeRecursive", "calling Merge() on '%s' with the corresponding object in '%s'",
							      obj->GetName(), key->GetName());
						}
					} else {
						while (nextsource) {
							// make sure we are at the correct directory level by cd'ing to path
							TDirectory *ndir = nextsource->GetDirectory(path);
							if (ndir) {
								ndir->cd();
								TKey *key2 = (TKey*)ndir->GetListOfKeys()->FindObject(key->GetName());
								if (key2) {
									TObject *hobj = key2->ReadObj();
									if (!hobj) {
										Info("MergeRecursive", "could not read object for key {%s, %s}; skipping file %s",
										     key->GetName(), key->GetTitle(), nextsource->GetName());
										nextsource = (TFile*)sourcelist->After(nextsource);
										continue;
									}
									// Set ownership for collections
									if (hobj->InheritsFrom(TCollection::Class())) {
										((TCollection*)hobj)->SetOwner();
									}
									hobj->ResetBit(kMustCleanup);
									listH.Add(hobj);
									Int_t error = 0;
									obj->Execute("Merge", listHargs.Data(), &error);
									info.fIsFirst = kFALSE;
									if (error) {
										Error("MergeRecursive", "calling Merge() on '%s' with the corresponding object in '%s'",
										      obj->GetName(), nextsource->GetName());
									}
									listH.Delete();
								}
							}
							nextsource = (TFile*)sourcelist->After( nextsource );
						}
						// Merge the list, if still to be done
						if (info.fIsFirst) {
							Int_t error = 0;
							obj->Execute("Merge", listHargs.Data(), &error);
							info.fIsFirst = kFALSE;
							listH.Delete();
						}
					}
				} else {
					// Object is of no type that we can merge
					canBeMerged = kFALSE;
				}
            
				// now write the merged histogram (which is "in" obj) to the target file
				// note that this will just store obj in the current directory level,
				// which is not persistent until the complete directory itself is stored
				// by "target->SaveSelf()" below
				target->cd();
            
				// only actually merge objects that can be merged (i.e. histograms and trees)
				if(!canBeMerged) continue;

				oldkeyname = key->GetName();
				//!!if the object is a tree, it is stored in globChain...
				if(obj->IsA()->InheritsFrom( TDirectory::Class() )) {
					//printf("cas d'une directory\n");
					// Do not delete the directory if it is part of the output
					// and we are in incremental mode (because it will be reuse
					// and has not been written to disk (for performance reason).
					// coverity[var_deref_model] the IsA()->InheritsFrom guarantees that the dynamic_cast will succeed. 
					if (dynamic_cast<TDirectory*>(obj)->GetFile() != target) {
						delete obj;
					}
				} else if (obj->IsA()->InheritsFrom( TCollection::Class() )) {
					// Don't overwrite, if the object were not merged.
					if ( obj->Write( oldkeyname, canBeMerged ? TObject::kSingleKey | TObject::kOverwrite : TObject::kSingleKey) <= 0 ) {
						status = kFALSE;
					}
					((TCollection*)obj)->SetOwner();
					delete obj;
				} else {
					// Don't overwrite, if the object were not merged.
					// NOTE: this is probably wrong for emulated objects.
					if ( obj->Write( oldkeyname, canBeMerged ? TObject::kOverwrite : 0) <= 0) {
						status = kFALSE;
					}
					cl->Destructor(obj); // just in case the class is not loaded.
				}
				info.Reset();
			} // while ( ( TKey *key = (TKey*)nextkey() ) )
		}
		current_file = current_file ? (TFile*)sourcelist->After(current_file) : (TFile*)sourcelist->First();
		if (current_file) {
			current_sourcedir = current_file->GetDirectory(path);
		} else {
			current_sourcedir = 0;
		}
	}
	// save modifications to the target directory.
	target->SaveSelf(kTRUE);

	return status;	
}


