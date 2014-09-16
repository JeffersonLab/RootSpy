
#include "RootSpy.h"
#include "rs_mainframe.h"
#include "rs_info.h"
#include "rs_cmsg.h"
#include "Dialog_SelectHists.h"
#include "Dialog_SaveHists.h"
#include "Dialog_IndivHists.h"
#include "Dialog_SelectTree.h"
#include "Dialog_ConfigMacros.h"
#include "Dialog_AskReset.h"
#include "Dialog_ScaleOpts.h"

#include <TROOT.h>
#include <TStyle.h>
#include <TApplication.h>
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

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <fstream>
using namespace std;



extern string ROOTSPY_UDL;
extern string CMSG_NAME;



// globals for drawing histograms
static TH1 *overlay_hist = NULL;
static TGaxis *overlay_yaxis = NULL;


// information for menu bar
enum MenuCommandIdentifiers {
  M_FILE_OPEN,
  M_FILE_SAVE,
  M_FILE_NEW_CONFIG,
  M_FILE_OPEN_CONFIG,
  M_FILE_SAVE_CONFIG,
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
rs_mainframe::rs_mainframe(const TGWindow *p, UInt_t w, UInt_t h,  bool build_gui):TGMainFrame(p,w,h, kMainFrame | kVerticalFrame)
{
	current_tab = NULL;

	//Define all of the -graphics objects. 
        if(build_gui) {
	    CreateGUI();

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
	
	delay_time = 4; // default is 4 seconds (needs to be tied to default used to set GUI)
	
	last_requested.hnamepath = "N/A";
	last_hist_plotted = NULL;
	
	dialog_selectserverhist = NULL;
	dialog_selecthists = NULL;
	dialog_savehists = NULL;
	dialog_selecttree = NULL;
	dialog_configmacros = NULL;
	dialog_indivhists = NULL;
	dialog_askreset = NULL;
	delete_dialog_selectserverhist = false;
	delete_dialog_selecthists = false;
	delete_dialog_savehists = false;
	delete_dialog_configmacros = false;
	delete_dialog_askreset = false;
	can_view_indiv = false;

	//overlay_mode = false;
	archive_file = NULL;

	//overlay_mode = true;
	//archive_file = new TFile("/u/home/sdobbs/test_archives/run1_output.root");

	SetWindowName("RootSpy");
	/** -- remove offline functionality for now
	// Finish up and map the window
	if(RS_CMSG->IsOnline())
	    SetWindowName("RootSpy - Online");
	else
	    SetWindowName("RootSpy - Offline");
	**/

	SetIconName("RootSpy");
	MapSubwindows();
	Resize(GetDefaultSize());
	MapWindow();

	viewStyle_rs = kViewByServer_rs;
	exec_shell = new TExec();

	// for testing
	//macro_files["/components/px"] = "macro.C";

	// Set ROOT style parameters
	//gROOT->SetStyle("Plain");
}

//-------------------
// Destructor
//-------------------
rs_mainframe::~rs_mainframe(void)
{

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
// ReadPreferences
//-------------------
void rs_mainframe::ReadPreferences(void)
{
	// Preferences file is "${HOME}/.RootSys"
	const char *home = getenv("HOME");
	if(!home)return;
	
	// Try and open file
	string fname = string(home) + "/.RootSys";
	ifstream ifs(fname.c_str());
	if(!ifs.is_open())return;
	cout<<"Reading preferences from \""<<fname<<"\" ..."<<endl;
	
	// Loop over lines
	char line[1024];
	while(!ifs.eof()){
		ifs.getline(line, 1024);
		if(strlen(line)==0)continue;
		if(line[0] == '#')continue;
		string str(line);
		
		// Break line into tokens
		vector<string> tokens;
		string buf; // Have a buffer string
		stringstream ss(str); // Insert the string into a stream
		while (ss >> buf)tokens.push_back(buf);
		if(tokens.size()<1)continue;

	}
	
	// close file
	ifs.close();
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
	string fname = string(home) + "/.RootSys";
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


	ofs<<endl;
	ofs.close();
	cout<<"Preferences written to \""<<fname<<"\""<<endl;
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
	gApplication->Terminate(0);	
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
// DoResetDialog
//-------------------
void rs_mainframe::DoResetDialog(void)
{

	if(!dialog_savehists){
		dialog_askreset = new Dialog_AskReset(gClient->GetRoot(), 300, 150);
	}else{
		dialog_askreset->RaiseWindow();
		dialog_askreset->RequestFocus();
	}
}

//-------------------
// DoSetScaleOptions
//-------------------
void rs_mainframe::DoSetScaleOptions(void)
{

	if(!dialog_savehists){
		dialog_scaleopts = new Dialog_ScaleOpts(gClient->GetRoot(), 300, 200);
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
	cout << "New tab selected id=" << id << "rstabs.size()=" << rstabs.size() << " current:0x" << hex << current_tab << dec << endl;
	list<RSTab*>::iterator it = rstabs.begin();
	advance(it, id);
	current_tab = *it;
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

//------------------
// DoLoopOverServers
//------------------
void rs_mainframe::DoLoopOverServers(void)
{
	RS_INFO->Lock();
	map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(RS_INFO->current.hnamepath);
	if(loop_over_servers->GetState()==kButtonUp && hdef_iter->second.hists.size() > 0){

		loop_over_hists->SetState(kButtonUp);
		
		_DBG_ << "I was here" << endl;	
			
		map<string, hinfo_t>::iterator hinfo_it = hdef_iter->second.hists.begin();	
		hinfo_it->second.setDisplayed(true);

	} else {
		loop_over_servers->SetState(kButtonDown);
		loop_over_hists->SetState(kButtonUp);
	}	
		
	RS_INFO->Unlock();
}

//------------------
// DoLoopOverHists
//------------------
void rs_mainframe::DoLoopOverHists(void)
{
	RS_INFO->Lock();
		
	map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(RS_INFO->current.hnamepath);
	if(loop_over_hists->GetState()==kButtonUp && hdef_iter->second.hists.size() > 0){
		loop_over_servers->SetState(kButtonUp);

		_DBG_ << "I was here" << endl;	

		map<string, hinfo_t>::iterator hinfo_it = hdef_iter->second.hists.begin();	
		while(hinfo_it!=hdef_iter->second.hists.end()) {
			_DBG_ << "I was here" << endl;	
			hinfo_it->second.setDisplayed(false);	
			hinfo_it++;
			_DBG_ << "I was here" << endl;
		}	hinfo_it->second.setDisplayed(false);

	} else {
		loop_over_hists->SetState(kButtonDown);
		loop_over_servers->SetState(kButtonUp);
	}		
	RS_INFO->Unlock();
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
	double now = RS_CMSG->GetTime();
	
	// Pings server to keep it alive
	if(now-last_ping_time >= 3.0){
		RS_CMSG->PingServers();
		last_ping_time = now;
	}
	
	// Ask for list of all hists from all servers every 5 seconds
	if(now-last_hist_requested >= 5.0){
		RS_CMSG->RequestHists("rootspy");
		last_hist_requested = now;
	}
	
	//	// Request histogram update if auto button is on. If either of the
	// "Loop over servers" or "Loop over hists" boxes are checked, we
	// call DoNext(), otherwise call DoUpdate(). If time the auto_refresh
	// time has not expired but the last requested hist is not what is
	// currently displayed, then go ahead and call DoUpdate() to make
	// the display current.
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

//	//selecthists dialog
//	if(delete_dialog_selecthists) {
//		dialog_selecthists = NULL;
//	}
//	delete_dialog_selecthists = false;
//
//	//savehists dialog
//	if(delete_dialog_savehists) {
//		dialog_savehists = NULL;
//	}
//	delete_dialog_savehists = false;
//	
//	//indivhists dialog
//	if(delete_dialog_indivhists) {
//		dialog_indivhists = NULL;
//	}
//	delete_dialog_indivhists = false;
//
//	//selecttree_dialog
//	if(delete_dialog_selecttree){
//		dialog_selecttree = NULL;
//	}
//	delete_dialog_selecttree = false;
//
//	//configmacros_dialog
//	if(delete_dialog_configmacros){
//		dialog_configmacros = NULL;
//	}
//	delete_dialog_configmacros = false;
//
//	//askreset_dialog
//	if(delete_dialog_askreset){
//		dialog_askreset = NULL;
//	}
//	delete_dialog_askreset = false;

//	// Update server label if necessary
//	if(selected_server){
//		string s = selected_server->GetTitle();
//		if(RS_INFO->servers.size() == 0){
//			s = "No servers";
//		} else if(RS_INFO->servers.size() == 1){
//			s = RS_INFO->current.serverName;
//		} else if(RS_INFO->servers.size() > 1){
//			stringstream ss;
//			ss << RS_INFO->servers.size();
//			s = "("; 
//			s += ss.str();  
//			s += " servers)";
//		}
//		selected_server->SetText(TString(s));
//	}
//	
//	// Update histo label if necessary
//	if(selected_hist){
//		string s = selected_hist->GetTitle();
//		if(RS_INFO->current.hnamepath=="") {
//		    selected_hist->SetText("-------------------------------------------------------");
//		} else if(s!=RS_INFO->current.hnamepath){
//		    selected_hist->SetText(RS_INFO->current.hnamepath.c_str());
//		}
//	}
//	// Update histo label if necessary
//	if(retrieved_lab){
//		string s = "";
//		if(RS_INFO->servers.size() == 0){
//			s = "No servers";
//		} else if(RS_INFO->servers.size() == 1){
//			s = "One server: " + RS_INFO->current.serverName;
//		} else if(RS_INFO->servers.size() > 1 && loop_over_servers->GetState() != kButtonDown){
//			s = "Several servers: ";
//			map<string, hdef_t>::iterator h_iter = RS_INFO->histdefs.begin();
//			for(; h_iter != RS_INFO->histdefs.end(); h_iter++){
//				if(h_iter->second.active) {
//					map<string, bool>::iterator s_iter = h_iter->second.servers.begin();
//					for(; s_iter != h_iter->second.servers.end(); s_iter++) {
//						if(s_iter->second){
//							s += s_iter->first;
//							s += " - ";	
//						}
//					}
//				}
//
//			}
//		}
//		retrieved_lab->SetText(TString(s));
//	}

	
//	// Request histogram update if auto button is on. If either of the
//	// "Loop over servers" or "Loop over hists" boxes are checked, we
//	// call DoNext(), otherwise call DoUpdate(). If time the auto_refresh
//	// time has not expired but the last requested hist is not what is
//	// currently displayed, then go ahead and call DoUpdate() to make
//	// the display current.
//	if(auto_refresh->GetState()==kButtonDown){
//		if((now-last_hist_requested) >= delay_time){
//			if(loop_over_servers->GetState()==kButtonDown) DoNext();
//			else if(loop_over_hists->GetState()==kButtonDown) DoNext();
//			else DoUpdate();
//		}else if(last_requested!=RS_INFO->current){
//			DoUpdate();
//		}
//	}
//	
//	// Request update of currently displayed histo if update flag is set
//	if(RS_INFO->update)DoUpdate();
//	
//	// Redraw histo if necessary
//	RS_INFO->Lock();
//	map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(RS_INFO->current.hnamepath);
//	
//	if(loop_over_servers->GetState()==kButtonDown && RS_INFO->servers.size() > 1 && hdef_iter->second.hists.size() > 0){
//		map<string, hinfo_t>::iterator hinfo_it = hdef_iter->second.hists.begin();
//		while(!hinfo_it->second.getDisplayed() && hinfo_it != hdef_iter->second.hists.end()) {
//				hinfo_it++;
//		}
//		
//		if(hinfo_it != hdef_iter->second.hists.end() && !hinfo_it->second.hasBeenDisplayed){
//		        canvas->cd();		
//			if(hdef_iter->second.type == hdef_t::macro) {
//				//_DBG_ << "DRAW MACRO" << endl;
//				DrawMacro(canvas, hinfo_it->second);
//			} else {
//				if(hinfo_it->second.hist != NULL) {
//				//_DBG_ << "Pointer to histogram was not NULL" << endl;
//				//hinfo_it->second.hist->Draw();
//					DrawHist(canvas, hinfo_it->second.hist, hinfo_it->second.hnamepath,
//						 hdef_iter->second.type, hdef_iter->second.display_info);  
//				} else {
//					//_DBG_ << "Pointer to histogram was NULL" << endl;
//				}
//			}
//			canvas->Modified();	
//			canvas->Update();
//			hinfo_it->second.hasBeenDisplayed = true;
//		}
//	} else if(hdef_iter!=RS_INFO->histdefs.end()){
//		if(hdef_iter->second.type == hdef_t::macro) {
//			// if we're looking at a macro, always execute it
//			//_DBG_ << "DRAW MACRO" << endl;
//			DrawMacro(canvas, hdef_iter->second);
//		} else if(hdef_iter->second.sum_hist_modified && hdef_iter->second.sum_hist!=NULL){
//			canvas->cd();
//			//hdef_iter->second.sum_hist->Draw();
//			DrawHist(canvas, hdef_iter->second.sum_hist, hdef_iter->second.hnamepath,
//				 hdef_iter->second.type, hdef_iter->second.display_info);
//			hdef_iter->second.sum_hist_modified = false;	// set flag indicating we've drawn current version
//			canvas->Modified();
//			canvas->Update();
//		}
//	}
//	RS_INFO->Unlock();
//
//	// Check when the last time we heard from each of the servers was
//	// and delete any that we haven't heard from in a while.
//	RS_INFO->Lock();
//	map<string,server_info_t>::iterator iter=RS_INFO->servers.begin();	
//	for(; iter!=RS_INFO->servers.end(); iter++){
//		time_t &last_heard_from = iter->second.lastHeardFrom;
//		if((now>=last_heard_from) && ((now-last_heard_from) > 10)){
//			cout<<"server: "<<iter->first<<" has gone away"<<endl;
//			RS_INFO->servers.erase(iter);
//		}
//	}
//	RS_INFO->Unlock();

	last_called = now;
}

//-------------------
// DoUpdate
//-------------------
void rs_mainframe::DoUpdate(void)
{
//	// Send a request to the server for an updated histogram
//	RS_INFO->Lock();
//	RS_INFO->update = false;
//	
//	bool request_sent = false;
//	
//	if(loop_over_servers->GetState()==kButtonDown){
//
//		map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(RS_INFO->current.hnamepath);
//		map<string, hinfo_t>::iterator hinfo_it = hdef_iter->second.hists.begin();
//
//		while(!hinfo_it->second.getDisplayed() && hinfo_it != hdef_iter->second.hists.end()) {
//				hinfo_it++;
//		}
//		
//		hinfo_it->second.hasBeenDisplayed = false;
//			
//		string &server = RS_INFO->current.serverName;
//		string &hnamepath = RS_INFO->current.hnamepath;
//		if(server!="" && hnamepath!=""){
//			if(hdef_iter->second.type == hdef_t::macro)
//				RS_CMSG->RequestMacro(server, hnamepath);
//			else
//				RS_CMSG->RequestHistogram(server, hnamepath);
//			request_sent = true;
//		}
//
//	}else if(RS_INFO->viewStyle==rs_info::kViewByObject){
//		// Loop over servers requesting this hist from each one (that is active)
//		map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(RS_INFO->current.hnamepath);
//		if(hdef_iter!=RS_INFO->histdefs.end()){
//			map<string, bool> &servers = hdef_iter->second.servers;
//			map<string, bool>::iterator server_iter = servers.begin();
//			for(; server_iter!=servers.end(); server_iter++){
//				if(server_iter->second){
//					if(hdef_iter->second.type == hdef_t::macro)
//						RS_CMSG->RequestMacro(server_iter->first, RS_INFO->current.hnamepath);
//					else
//						RS_CMSG->RequestHistogram(server_iter->first, RS_INFO->current.hnamepath);
//					request_sent = true;
//				}
//			}
//		}
//	}else{
//		// Request only a single histogram
//		string &server = RS_INFO->current.serverName;
//		string &hnamepath = RS_INFO->current.hnamepath;
//		if(server!="" && hnamepath!=""){
//			map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(RS_INFO->current.hnamepath);
//			if(hdef_iter->second.type == hdef_t::macro)
//				RS_CMSG->RequestMacro(server, hnamepath);
//			else 
//				RS_CMSG->RequestHistogram(server, hnamepath);
//			request_sent = true;
//		}
//	}
//	
//	if(request_sent){
//		time_t now = time(NULL);
//		last_requested = RS_INFO->current;
//	}
//
//	RS_INFO->Unlock();
}

//----------
//DoSetArchiveFile
//----------
//add comment
void rs_mainframe::DoSetArchiveFile(void) {
	TGFileInfo* fileinfo = new TGFileInfo();
	new TGFileDialog(gClient->GetRoot(), gClient->GetRoot(), kFDOpen, fileinfo);

	if(archive_file)
	    archive_file->Close();

	archive_file = new TFile(fileinfo->fFilename);
	// check for errors?

	// update display on GUI
	stringstream ss;
	ss << "Archive file: " << fileinfo->fFilename;
	//archive_filename->SetTitle(ss.str().c_str());
	archive_filename->SetText(ss.str().c_str());
	//archive_filename->Resize(300,30);

	cout << "loaded archiver file = " << ss.str() << endl;
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
			RS_CMSG->RequestTreeInfo(servername);
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
  /**
  while(!ifs.eof()) {
    hid_t hid;
    ifs >> hid;

    if( (hid.hnamepath == "") || (hid.serverName=="") )
      break;

    _DBG_ << hid << endl;
    hids.push_back( hid );
  }
  **/

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
  for(map<string,server_info_t>::iterator server_iter = RS_INFO->servers.begin();                            server_iter != RS_INFO->servers.end(); server_iter++) {         
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
  
  /**
  // now load the histograms that we want to display
  // we do this differently depending on which view model we are using
  for( vector<hid_t>::const_iterator hid_it = hids.begin();
       hid_it != hids.end(); hid_it++ ) {

    map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(hid_it->hnamepath);
    if(hdef_iter==RS_INFO->histdefs.end()) continue;
    
    if(RS_INFO->viewStyle == rs_info::kViewByObject) {
      hdef_iter->second.active = true;
      
      // allow a wildcard "*" to select all servers
      if(hid_it->serverName != "*") {
	hdef_iter->second.servers[hid_it->serverName] = true;
      } else {
	for(map<string, bool>::iterator hdefserver_iter = hdef_iter->second.servers.begin();
	    hdefserver_iter != hdef_iter->second.servers.end(); hdefserver_iter++) {
	  hdefserver_iter->second = true;
	}
      }
    } else {  // RS_INFO->viewStyle == rs_info::kViewByServer
      map<string,server_info_t>::iterator server_info_iter = RS_INFO->servers.find(hid_it->serverName);
      if(server_info_iter==RS_INFO->servers.end()) continue;

      server_info_iter->second.active = true;
      hdef_iter->second.servers[hid_it->serverName] = true;
    }
  }
  **/

  // is setting current working right?

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

  RSMF->can_view_indiv = true;
  //RS_INFO->viewStyle = viewStyle;
  RS_INFO->update = true;
  
  RS_INFO->Unlock();

}

//-------------------
// DoSaveHistsList
//-------------------
void rs_mainframe::DoSaveHistsList(void)
{
  // get list of histo / server combinations
  //vector<hid_t> hids;
  //map<string, bool> hists_tosave;
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

  /*
  // Make list of all histograms from all servers
  map<string,server_info_t>::iterator server_info_iter = RS_INFO->servers.begin();
  for(; server_info_iter!=RS_INFO->servers.end(); server_info_iter++){//iterates over servers
    const string &server = server_info_iter->first;
    const vector<string> &hnamepaths = server_info_iter->second.hnamepaths;
    for(unsigned int j=0; j<hnamepaths.size(); j++){//iterates over histogram paths

      // add this hinfo_t object to the list, if it's part of the active set
      if(RS_INFO->histdefs[hnamepaths[j]].active) {
	hids.push_back(hid_t(server, hnamepaths[j]));
      }

    }
  }
  */

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
		RS_CMSG->FinalHistogram(server, paths);
		
	}
}

//-------------------
// DoOnline
//-------------------
void rs_mainframe::DoOnline(void)
{
    if(!RS_CMSG->IsOnline()) {
	// Create cMsg object
	char hostname[256];
	gethostname(hostname, 256);
	char str[512];
	sprintf(str, "RootSpy GUI %s-%d", hostname, getpid());
	CMSG_NAME = string(str);
	cout << "Full UDL is " << ROOTSPY_UDL << endl;

	delete RS_CMSG;
	RS_CMSG = new rs_cmsg(ROOTSPY_UDL, CMSG_NAME);
    }

    if(RS_CMSG->IsOnline())
	SetWindowName("RootSpy - Online");
    else
	SetWindowName("RootSpy - Offline");
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
   fMenuFile->AddEntry("Save Configuration...", M_FILE_SAVE_CONFIG);
   fMenuFile->AddEntry("E&xit", M_FILE_EXIT);

   fMenuTools = new TGPopupMenu(gClient->GetRoot());
   fMenuTools->AddEntry("Config Macros...", M_TOOLS_MACROS);
   fMenuTools->AddEntry("Start TBrowser", M_TOOLS_TBROWSER);
   fMenuTools->AddEntry("View Tree Info", M_TOOLS_TREEINFO);
   fMenuTools->AddEntry("Save Hists...",  M_TOOLS_SAVEHISTS);
   fMenuTools->AddSeparator();
   fMenuTools->AddEntry("Reset Servers...",  M_TOOLS_RESET);
   
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
	string udl_label = "UDL = "+ROOTSPY_UDL;
	lUDL = AddLabel(fMainTopLeft, udl_label);
	
	// Buttons
	TGHorizontalFrame *fMainTopLeftButtons = new TGHorizontalFrame(fMainTopLeft);
	fMainTopLeft->AddFrame(fMainTopLeftButtons, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

	TGTextButton *bSetArchive = AddButton(fMainTopLeftButtons, "Set Archive");
	
	// Update options
	bAutoRefresh = AddCheckButton(fMainTopRightOptions, "Auto-refresh");
	bAutoAdvance = AddCheckButton(fMainTopRightOptions, "Auto-advance");
	bAutoRefresh->SetState(kButtonDown);

   // Delay
   TGHorizontalFrame *fMainTopRightDelay = new TGHorizontalFrame(fMainTopRight);
   AddLabel(fMainTopRightDelay, "delay:");
   TGComboBox *fDelay = new TGComboBox(fMainTopRightDelay,"-",-1,kHorizontalFrame | kSunkenFrame | kDoubleBorder | kOwnBackground);
   fDelay->AddEntry("0s",0);
   fDelay->AddEntry("1s",1);
   fDelay->AddEntry("2s",2);
   fDelay->AddEntry("3s",3);
   fDelay->AddEntry("4s",4);
   fDelay->AddEntry("10s ",10);
   fDelay->Resize(50,22);
   fDelay->Select(4);
   fDelay->GetTextEntry()->SetText("4s");
   fMainTopRightDelay->AddFrame(fDelay, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   fMainTopRight->AddFrame(fMainTopRightDelay, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
	
	//....... Tabs .......
	// Add a single tab for now
	new RSTab(this, "New");

	//....... Bottom Frame .......
	AddSpacer(fMainBot, 50, 1, kLHintsRight);
	TGTextButton *bQuit = AddButton(fMainBot, "Quit", kLHintsRight);

	fMain->MapSubwindows();
	fMain->MapWindow();

	//==================== Connect GUI elements to methods ====================
	bQuit->Connect("Clicked()","rs_mainframe", this, "DoQuit()");
	fMainTab->Connect("Selected(Int_t)", "rs_mainframe", this, "DoTabSelected(Int_t)");
	bSetArchive->Connect("Clicked()","rs_mainframe", this, "DoSetArchiveFile()");
	fDelay->Connect("Selected(Int_t)","rs_mainframe", this, "DoSelectDelay(Int_t)");

#if 0
	//======================= The following was copied for a macro made with TGuiBuilder ===============

	TGMainFrame *fMainFrame1435 = this;
	   // composite frame
   TGCompositeFrame *fMainFrame656 = new TGCompositeFrame(fMainFrame1435,833,700,kVerticalFrame);

   // composite frame
   TGCompositeFrame *fCompositeFrame657 = new TGCompositeFrame(fMainFrame656,833,700,kVerticalFrame);

   // composite frame
   TGCompositeFrame *fCompositeFrame658 = new TGCompositeFrame(fCompositeFrame657,833,700,kVerticalFrame);

   // composite frame
   TGCompositeFrame *fCompositeFrame659 = new TGCompositeFrame(fCompositeFrame658,833,700,kVerticalFrame);
//   fCompositeFrame659->SetLayoutBroken(kTRUE);

   // composite frame
   TGCompositeFrame *fCompositeFrame660 = new TGCompositeFrame(fCompositeFrame659,833,700,kVerticalFrame);
//   fCompositeFrame660->SetLayoutBroken(kTRUE);

   // composite frame
   TGCompositeFrame *fCompositeFrame661 = new TGCompositeFrame(fCompositeFrame660,833,700,kVerticalFrame);
//   fCompositeFrame661->SetLayoutBroken(kTRUE);

   // vertical frame
   TGVerticalFrame *fVerticalFrame662 = new TGVerticalFrame(fCompositeFrame661,708,640,kVerticalFrame);
//   fVerticalFrame662->SetLayoutBroken(kTRUE);

   // horizontal frame
   TGHorizontalFrame *fHorizontalFrame663 = new TGHorizontalFrame(fVerticalFrame662,564,118,kHorizontalFrame);

   // "Current Histogram Info." group frame
   TGGroupFrame *fGroupFrame664 = new TGGroupFrame(fHorizontalFrame663,"Current Histogram Info.",kHorizontalFrame | kRaisedFrame);

   // vertical frame
   TGVerticalFrame *fVerticalFrame665 = new TGVerticalFrame(fGroupFrame664,62,60,kVerticalFrame);
   TGLabel *fLabel666 = new TGLabel(fVerticalFrame665,"Server:");
   fLabel666->SetTextJustify(kTextRight);
   fLabel666->SetMargins(0,0,0,0);
   fLabel666->SetWrapLength(-1);
   fVerticalFrame665->AddFrame(fLabel666, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));
   TGLabel *fLabel667 = new TGLabel(fVerticalFrame665,"Histogram:");
   fLabel667->SetTextJustify(kTextRight);
   fLabel667->SetMargins(0,0,0,0);
   fLabel667->SetWrapLength(-1);
   fVerticalFrame665->AddFrame(fLabel667, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));
   TGLabel *fLabel668 = new TGLabel(fVerticalFrame665,"Retrieved:");
   fLabel668->SetTextJustify(kTextRight);
   fLabel668->SetMargins(0,0,0,0);
   fLabel668->SetWrapLength(-1);
   fVerticalFrame665->AddFrame(fLabel668, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));


   TGTextButton *fTextButtonIndiv = new TGTextButton(fVerticalFrame665,"View Indiv.");
   fTextButtonIndiv->SetTextJustify(36);
   fTextButtonIndiv->SetMargins(0,0,0,0);
   fTextButtonIndiv->SetWrapLength(-1);
   fTextButtonIndiv->Resize(100,22);
   fVerticalFrame665->AddFrame(fTextButtonIndiv, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

/*
   TGRadioButton *fRadioButton019 = new TGRadioButton(fVerticalFrame665,"View By Server");
   fRadioButton019->SetState(kButtonDown);
   fRadioButton019->SetTextJustify(36);
   fRadioButton019->SetMargins(0,0,0,0);
   fRadioButton019->SetWrapLength(-1);
   fVerticalFrame665->AddFrame(fRadioButton019, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
*/
   //fGroupFrame664->AddFrame(fVerticalFrame665, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX | kFitWidth,2,2,2,2));
   fGroupFrame664->AddFrame(fVerticalFrame665, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX ,2,2,2,2));

   // vertical frame
   TGVerticalFrame *fVerticalFrame669 = new TGVerticalFrame(fGroupFrame664,29,60,kVerticalFrame);
   TGLabel *fLabel670 = new TGLabel(fVerticalFrame669,"------------------------------");
   fLabel670->SetTextJustify(kTextLeft);
   fLabel670->SetMargins(0,0,0,0);
   fLabel670->SetWrapLength(-1);
   fVerticalFrame669->AddFrame(fLabel670, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   TGLabel *fLabel671 = new TGLabel(fVerticalFrame669,"-------------------------------------------------------");
   fLabel671->SetTextJustify(kTextLeft);
   fLabel671->SetMargins(0,0,0,0);
   fLabel671->SetWrapLength(-1);
   fVerticalFrame669->AddFrame(fLabel671, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   TGLabel *fLabel672 = new TGLabel(fVerticalFrame669,"-----test-------------------------");
   fLabel672->SetTextJustify(kTextLeft);
   fLabel672->SetMargins(0,0,0,0);
   fLabel672->SetWrapLength(-1);
   fVerticalFrame669->AddFrame(fLabel672, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fGroupFrame664->AddFrame(fVerticalFrame669, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));

   // vertical frame
   TGVerticalFrame *fVerticalFrame673 = new TGVerticalFrame(fGroupFrame664,97,78,kVerticalFrame);

   TGHorizontalFrame *fHorizontalFrame801 = new TGHorizontalFrame(fVerticalFrame673,97,24,kHorizontalFrame);
   TGTextButton *fTextButton674 = new TGTextButton(fHorizontalFrame801,"Select Server/Histo");
   fTextButton674->SetTextJustify(36);
   fTextButton674->SetMargins(0,0,0,0);
   fTextButton674->SetWrapLength(-1);
   fTextButton674->Resize(93,22);
   fHorizontalFrame801->AddFrame(fTextButton674, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   fVerticalFrame673->AddFrame(fHorizontalFrame801, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   TGHorizontalFrame *fHorizontalFrame802 = new TGHorizontalFrame(fVerticalFrame673,97,24,kHorizontalFrame);
   TGTextButton *fTextButton677 = new TGTextButton(fHorizontalFrame802,"Prev");
   fTextButton677->SetTextJustify(36);
   fTextButton677->SetMargins(0,0,0,0);
   fTextButton677->SetWrapLength(-1);
   fTextButton677->Resize(87,22);
   fHorizontalFrame802->AddFrame(fTextButton677, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   TGTextButton *fTextButton675 = new TGTextButton(fHorizontalFrame802,"Next");
   fTextButton675->SetTextJustify(36);
   fTextButton675->SetMargins(0,0,0,0);
   fTextButton675->SetWrapLength(-1);
   fTextButton675->Resize(87,22);
   fHorizontalFrame802->AddFrame(fTextButton675, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   fVerticalFrame673->AddFrame(fHorizontalFrame802, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   TGHorizontalFrame *fHorizontalFrame803 = new TGHorizontalFrame(fVerticalFrame673,97,24,kHorizontalFrame);
   TGTextButton *fTextButton676 = new TGTextButton(fHorizontalFrame803,"Update");
   fTextButton676->SetTextJustify(36);
   fTextButton676->SetMargins(0,0,0,0);
   fTextButton676->SetWrapLength(-1);
   fTextButton676->Resize(87,22);
   fHorizontalFrame803->AddFrame(fTextButton676, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fVerticalFrame673->AddFrame(fHorizontalFrame803, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
//   TGTextButton *fSelectTree = new TGTextButton(fVerticalFrame673, "Tree Info");
//   fSelectTree->SetTextJustify(36);
//   fSelectTree->SetMargins(0,0,0,0);
//   fSelectTree->SetWrapLength(-1);
//   fSelectTree->Resize(10,10);
//   fVerticalFrame673->AddFrame(fTextButton676, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fGroupFrame664->AddFrame(fVerticalFrame673, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fGroupFrame664->SetLayoutManager(new TGHorizontalLayout(fGroupFrame664));
   fGroupFrame664->Resize(232,114);
   fHorizontalFrame663->AddFrame(fGroupFrame664, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));

   // "fGroupFrame746" group frame
   TGGroupFrame *fGroupFrame746 = new TGGroupFrame(fHorizontalFrame663,"Continuous Update options",kHorizontalFrame);

   // vertical frame
   TGVerticalFrame *fVerticalFrame678 = new TGVerticalFrame(fGroupFrame746,144,63,kVerticalFrame);
   TGCheckButton *fCheckButton679 = new TGCheckButton(fVerticalFrame678,"Auto-refresh");
   fCheckButton679->SetTextJustify(36);
   fCheckButton679->SetMargins(0,0,0,0);
   fCheckButton679->SetWrapLength(-1);
   fVerticalFrame678->AddFrame(fCheckButton679, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   TGCheckButton *fCheckButton680 = new TGCheckButton(fVerticalFrame678,"loop over all servers");
   fCheckButton680->SetTextJustify(36);
   fCheckButton680->SetMargins(0,0,0,0);
   fCheckButton680->SetWrapLength(-1);
   fVerticalFrame678->AddFrame(fCheckButton680, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   TGCheckButton *fCheckButton681 = new TGCheckButton(fVerticalFrame678,"loop over all hists");
   fCheckButton681->SetTextJustify(36);
   fCheckButton681->SetMargins(0,0,0,0);
   fCheckButton681->SetWrapLength(-1);
   fVerticalFrame678->AddFrame(fCheckButton681, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   TGCheckButton *fCheckButton6681 = new TGCheckButton(fVerticalFrame678,"show archived hists");
   fCheckButton6681->SetTextJustify(36);
   fCheckButton6681->SetMargins(0,0,0,0);
   fCheckButton6681->SetWrapLength(-1);
   fVerticalFrame678->AddFrame(fCheckButton6681, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fGroupFrame746->AddFrame(fVerticalFrame678, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   // horizontal frame
   TGHorizontalFrame *fHorizontalFrame682 = new TGHorizontalFrame(fGroupFrame746,144,26,kHorizontalFrame);
   TGLabel *fLabel683 = new TGLabel(fHorizontalFrame682,"delay:");
   fLabel683->SetTextJustify(36);
   fLabel683->SetMargins(0,0,0,0);
   fLabel683->SetWrapLength(-1);
   fHorizontalFrame682->AddFrame(fLabel683, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   ULong_t ucolor;        // will reflect user color changes
   gClient->GetColorByName("#ffffff",ucolor);

   // combo box
   TGComboBox *fComboBox684 = new TGComboBox(fHorizontalFrame682,"-",-1,kHorizontalFrame | kSunkenFrame | kDoubleBorder | kOwnBackground);
   fComboBox684->AddEntry("0s",0);
   fComboBox684->AddEntry("1s",1);
   fComboBox684->AddEntry("2s",2);
   fComboBox684->AddEntry("3s",3);
   fComboBox684->AddEntry("4s",4);
   fComboBox684->AddEntry("10s ",10);
   fComboBox684->Resize(50,22);
   fComboBox684->Select(-1);
   fHorizontalFrame682->AddFrame(fComboBox684, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fGroupFrame746->AddFrame(fHorizontalFrame682, new TGLayoutHints(kLHintsNormal));

   fGroupFrame746->SetLayoutManager(new TGHorizontalLayout(fGroupFrame746));
   fGroupFrame746->Resize(324,99);
   fHorizontalFrame663->AddFrame(fGroupFrame746, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fVerticalFrame662->AddFrame(fHorizontalFrame663, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   fHorizontalFrame663->MoveResize(2,2,564,118);

   // embedded canvas
   TRootEmbeddedCanvas *fRootEmbeddedCanvas698 = new TRootEmbeddedCanvas(0,fVerticalFrame662,704,432);
   Int_t wfRootEmbeddedCanvas698 = fRootEmbeddedCanvas698->GetCanvasWindowId();
   TCanvas *c125 = new TCanvas("c125", 10, 10, wfRootEmbeddedCanvas698);
   fRootEmbeddedCanvas698->AdoptCanvas(c125);
   fVerticalFrame662->AddFrame(fRootEmbeddedCanvas698, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight,2,2,2,2));
   fRootEmbeddedCanvas698->MoveResize(2,124,704,432);

   // horizontal frame
   TGHorizontalFrame *fHorizontalFrame1060 = new TGHorizontalFrame(fVerticalFrame662,704,82,kHorizontalFrame);

   // "fGroupFrame750" group frame
   TGGroupFrame *fGroupFrame711 = new TGGroupFrame(fHorizontalFrame1060,"cMsg Info.");
   //TGLabel *fLabel1030 = new TGLabel(fGroupFrame711,"UDL = cMsg://127.0.0.1/cMsg/rootspy");
   //string udl_label = "UDL = "+ROOTSPY_UDL;
   TGLabel *fLabel1030 = new TGLabel(fGroupFrame711,udl_label.c_str());
   fLabel1030->SetTextJustify(36);
   fLabel1030->SetMargins(0,0,0,0);
   fLabel1030->SetWrapLength(-1);
   fGroupFrame711->AddFrame(fLabel1030, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));


   TGHorizontalFrame *fHorizontalFrame690 = new TGHorizontalFrame(fGroupFrame711,400,50,kHorizontalFrame);
   /*
   // Get online button added by sdobbs 4/22/13
   TGTextButton *fTextButtonOnline = new TGTextButton(fHorizontalFrame690,"Online");
   //TGTextButton *fTextButtonOnline = new TGTextButton(fGroupFrame711,"Online");
   fTextButtonOnline->SetTextJustify(36);
   fTextButtonOnline->SetMargins(0,0,0,0);
   fTextButtonOnline->SetWrapLength(-1);
   fTextButtonOnline->Resize(150,22);
   fHorizontalFrame690->AddFrame(fTextButtonOnline, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   //fGroupFrame711->AddFrame(fTextButtonOnline, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   //fGroupFrame711->AddFrame(fTextButtonOnline, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   */

   TGTextButton *fTextButton1041 = new TGTextButton(fHorizontalFrame690,"modify");
   //TGTextButton *fTextButton1041 = new TGTextButton(fGroupFrame711,"modify");
   fTextButton1041->SetTextJustify(36);
   fTextButton1041->SetMargins(0,0,0,0);
   fTextButton1041->SetWrapLength(-1);
   fTextButton1041->Resize(97,22);
   fHorizontalFrame690->AddFrame(fTextButton1041, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));
   //fGroupFrame711->AddFrame(fTextButton1041, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fHorizontalFrame690->SetLayoutManager(new TGHorizontalLayout(fHorizontalFrame690));
   fGroupFrame711->AddFrame(fHorizontalFrame690, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   fGroupFrame711->SetLayoutManager(new TGVerticalLayout(fGroupFrame711));
   fGroupFrame711->Resize(133,78);
   fHorizontalFrame1060->AddFrame(fGroupFrame711, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));


   TGVerticalFrame *fVerticalFrame3673 = new TGVerticalFrame(fHorizontalFrame1060,400,78,kVerticalFrame);
   TGHorizontalFrame *fHorizontalFrame2060 = new TGHorizontalFrame(fVerticalFrame3673,400,30,kHorizontalFrame);

   //TGTextButton *fTextButton1075 = new TGTextButton(fHorizontalFrame1060,"Quit");
   TGTextButton *fTextButton1075 = new TGTextButton(fHorizontalFrame2060,"Quit");
   fTextButton1075->SetTextJustify(36);
   fTextButton1075->SetMargins(0,0,0,0);
   fTextButton1075->SetWrapLength(-1);
   fTextButton1075->Resize(97,22);
   //fHorizontalFrame1060->AddFrame(fTextButton1075, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   fHorizontalFrame2060->AddFrame(fTextButton1075, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   /**
   TGTextButton *fTextButtonTB = new TGTextButton(fHorizontalFrame1060,"TBrowser");
   fTextButtonTB->SetTextJustify(36);
   fTextButtonTB->SetMargins(0,0,0,0);
   fTextButtonTB->SetWrapLength(-1);
   fTextButtonTB->Resize(150,22);
   fHorizontalFrame1060->AddFrame(fTextButtonTB, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   **/
   //Save Canvas button added by Justinb 6.10.10
   //TGTextButton *fTextButtonSave = new TGTextButton(fHorizontalFrame1060,"Save Canvas");
   TGTextButton *fTextButtonSave = new TGTextButton(fHorizontalFrame2060,"Save Canvas");
   fTextButtonSave->SetTextJustify(36);
   fTextButtonSave->SetMargins(0,0,0,0);
   fTextButtonSave->SetWrapLength(-1);
   fTextButtonSave->Resize(200,22);
   //fHorizontalFrame1060->AddFrame(fTextButtonSave, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   fHorizontalFrame2060->AddFrame(fTextButtonSave, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   /**
   //Save Hists button added by Justinb 6.10.10
   TGTextButton *fTextButtonSaveHists = new TGTextButton(fHorizontalFrame1060,"Save Hists");
   fTextButtonSaveHists->SetTextJustify(36);
   fTextButtonSaveHists->SetMargins(0,0,0,0);
   fTextButtonSaveHists->SetWrapLength(-1);
   fTextButtonSaveHists->Resize(250,22);
   fHorizontalFrame1060->AddFrame(fTextButtonSaveHists, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));

   //Save Hists button added by Justinb 6.10.10
   TGTextButton *fTextButtonfinal = new TGTextButton(fHorizontalFrame1060,"finals");
   fTextButtonfinal->SetTextJustify(36);
   fTextButtonfinal->SetMargins(0,0,0,0);
   fTextButtonfinal->SetWrapLength(-1);
   fTextButtonfinal->Resize(300,22);
   fHorizontalFrame1060->AddFrame(fTextButtonfinal, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
  
   TGTextButton *fTextButton1297 = new TGTextButton(fHorizontalFrame1060,"Tree Info");
   fTextButton1297->SetTextJustify(36);
   fTextButton1297->SetMargins(0,0,0,0);
   fTextButton1297->SetWrapLength(-1);
   fTextButton1297->Resize(97,22);
   fHorizontalFrame1060->AddFrame(fTextButton1297, new TGLayoutHints(kLHintsNormal));
   **/
   //TGTextButton *fTextButtonSetArchive = new TGTextButton(fHorizontalFrame1060,"Set Archive");
   TGTextButton *fTextButtonSetArchive = new TGTextButton(fHorizontalFrame2060,"Set Archive");
   fTextButtonSetArchive->SetTextJustify(36);
   fTextButtonSetArchive->SetMargins(0,0,0,0);
   fTextButtonSetArchive->SetWrapLength(-1);
   fTextButtonSetArchive->Resize(300,22);
   //fHorizontalFrame1060->AddFrame(fTextButtonSetArchive, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   fHorizontalFrame2060->AddFrame(fTextButtonSetArchive, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));

   TGTextButton *fTextButtonTBrowser = new TGTextButton(fHorizontalFrame2060,"TBrowser");
   fTextButtonSetArchive->SetTextJustify(36);
   fTextButtonSetArchive->SetMargins(0,0,0,0);
   fTextButtonSetArchive->SetWrapLength(-1);
   fTextButtonSetArchive->Resize(300,22);
   fHorizontalFrame2060->AddFrame(fTextButtonTBrowser, new TGLayoutHints(kLHintsLeft | kLHintsTop,2,2,2,2));

   TGHorizontalFrame *fHorizontalFrame3060 = new TGHorizontalFrame(fVerticalFrame3673,400,30,kHorizontalFrame);

   //TGLabel *fLabel2030 = new TGLabel(fHorizontalFrame1060,"");
   TGLabel *fLabel2030 = new TGLabel(fHorizontalFrame3060,"");
   fLabel2030->SetMargins(0,0,0,0);
   fLabel2030->SetWrapLength(-1);
   fLabel2030->SetTextJustify(kTextTop | kTextRight);
   //fLabel2030->Resize(300,30);
   //fHorizontalFrame1060->AddFrame(fLabel2030, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   //fHorizontalFrame3060->AddFrame(fLabel2030, new TGLayoutHints(kLHintsRight | kLHintsTop,2,2,2,2));
   fHorizontalFrame3060->AddFrame(fLabel2030, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX,2,2,2,2));


   fVerticalFrame3673->AddFrame(fHorizontalFrame2060, new TGLayoutHints(kLHintsRight | kLHintsTop | kLHintsExpandX,2,2,2,2));
   fVerticalFrame3673->AddFrame(fHorizontalFrame3060, new TGLayoutHints(kLHintsRight | kLHintsTop | kLHintsExpandX,2,2,2,2));
   
   fHorizontalFrame1060->AddFrame(fVerticalFrame3673, new TGLayoutHints(kLHintsRight | kLHintsTop | kLHintsExpandX,2,2,2,2));


   //fVerticalFrame662->AddFrame(fHorizontalFrame1060, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight,2,2,2,2));
   fVerticalFrame662->AddFrame(fHorizontalFrame1060, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX ,2,2,2,2));
   //fHorizontalFrame1060->MoveResize(2,560,704,82);

   fCompositeFrame661->AddFrame(fVerticalFrame662, new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight,2,2,2,2));
   //fVerticalFrame662->MoveResize(2,2,708,640);

   fCompositeFrame660->AddFrame(fCompositeFrame661, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight));
   //fCompositeFrame661->MoveResize(0,0,833,700);

   fCompositeFrame659->AddFrame(fCompositeFrame660, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight));
   //fCompositeFrame660->MoveResize(0,0,833,700);

   fCompositeFrame658->AddFrame(fCompositeFrame659, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight));

   fCompositeFrame657->AddFrame(fCompositeFrame658, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight));

   fMainFrame656->AddFrame(fCompositeFrame657, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight));

// Temporarily disable old window contents
//   fMainFrame1435->AddFrame(fMainFrame656, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY | kFitWidth | kFitHeight));

   fMainFrame1435->SetMWMHints(kMWMDecorAll,
                        kMWMFuncAll,
                        kMWMInputModeless);
   fMainFrame1435->MapSubwindows();

   //fMainFrame1435->Resize(fMainFrame1435->GetDefaultSize());
   fMainFrame1435->MapWindow();
   fMainFrame1435->Resize(833,700);


   //==============================================================================================

	// Connect GUI elements to methods
//	TGTextButton* &quit = fTextButton1075;
//	TGTextButton* &quit = bQuit;
	//TGTextButton* &TB = fTextButtonTB;
//	TGTextButton* &save = fTextButtonSave; //added Justin.b 06.10.10
//	//TGTextButton* &savehists = fTextButtonSaveHists; //added Justin.b 06.15.10
//	TGTextButton* &selectserver = fTextButton674;
//	//TGTextButton* &treeinfobutton = fTextButton1297;
//	//TGTextButton* &final = fTextButtonfinal;
//	TGTextButton* &setarchive = fTextButtonSetArchive;
//	TGTextButton* &tbrowser = fTextButtonTBrowser;
//	//TGTextButton* &online = fTextButtonOnline;
//	indiv = fTextButtonIndiv;

	//this->VBServer = fRadioButton019;
	selected_server = fLabel670;
	selected_hist = fLabel671;
	retrieved_lab = fLabel672;
	archive_filename = fLabel2030;
	delay = fComboBox684;
	canvas = fRootEmbeddedCanvas698->GetCanvas();
	auto_refresh = fCheckButton679;
	loop_over_servers = fCheckButton680;
	loop_over_hists = fCheckButton681;
	show_archived_hists = fCheckButton6681;
//	TGTextButton* &update = fTextButton676;
//	TGTextButton* &next = fTextButton675;
//	TGTextButton* &prev = fTextButton677;
//	TGTextButton* &modify = fTextButton1041;
#endif
	

	//TB->Connect("Clicked()","rs_mainframe", this, "MakeTB()");
//        save->Connect("Clicked()", "rs_mainframe", this, "DoSave()");//added Justin.b 06.10.10
	//savehists->Connect("Clicked()", "rs_mainframe", this, "DoSaveHists()");//added Justin.b 06.10.15
//	selectserver->Connect("Clicked()","rs_mainframe", this, "DoSelectHists()");
//	indiv->Connect("Clicked()","rs_mainframe", this, "DoIndiv()");
	//this->VBServer->Connect("Clicked()","rs_mainframe", this, "DoSetVBServer()");
//	next->Connect("Clicked()","rs_mainframe", this, "DoNext()");
//	prev->Connect("Clicked()","rs_mainframe", this, "DoPrev()");

	//online->Connect("Clicked()", "rs_mainframe", this, "DoOnline()");
	//final->Connect("Clicked()", "rs_mainframe", this, "DoFinal()");
	//treeinfobutton->Connect("Clicked()", "rs_mainframe", this, "DoTreeInfo()");
//	setarchive->Connect("Clicked()", "rs_mainframe", this, "DoSetArchiveFile()");
//	tbrowser->Connect("Clicked()", "rs_mainframe", this, "DoTBrowser()");
//	update->Connect("Clicked()","rs_mainframe", this, "DoUpdate()");
//	delay->Select(4, kTRUE);
//	delay->GetTextEntry()->SetText("4s");
//	delay->Connect("Selected(Int_t)","rs_mainframe", this, "DoSelectDelay(Int_t)");
//	
//	loop_over_servers->Connect("Clicked()","rs_mainframe",this, "DoLoopOverServers()");
//	loop_over_hists->Connect("Clicked()","rs_mainframe",this, "DoLoopOverHists()");
//	
//	modify->SetEnabled(kFALSE);
//	
//	canvas->SetFillColor(kWhite);

}



/**
// helper function
// move this somewhere else?
static void add_to_draw_hist_args(string &args, string toadd) 
{
    // we have to be careful about where we put spaces since ROOT is really picky about argument formatting
    if(args == "") 
	args = toadd;
    else 
	args = args + " " + toadd;	    
}
**/

// wrapper for histogram drawing
void rs_mainframe::DrawHist(TCanvas *the_canvas, TH1 *hist, string hnamepath,
			    hdef_t::histdimension_t hdim,
			    hdisplay_info_t &display_info)
{
    string histdraw_args = "";
    bool overlay_enabled = (show_archived_hists->GetState()==kButtonDown);
    //bool overlay_enabled = false;
    //double hist_line_width = 1.;
    float overlay_ymin=0., overlay_ymax=0.;
    
    //the_canvas->Divide(1);
    the_canvas->cd(0);

    if(display_info.use_logx)
	the_canvas->SetLogx();
    else
	the_canvas->SetLogx(0);
    if(display_info.use_logy)
	the_canvas->SetLogy();
    else
	the_canvas->SetLogy(0);

    if(hdim == hdef_t::oneD) {
	
      //if(hdim == hdef_t::histdimension_t::oneD) {
	bool do_overlay = false;
	//TH1 *archived_hist = NULL;

	// check for archived histograms and load them if they exist and we are overlaying
	// we do this first to determine the parameters needed to overlay the histograms
	if(overlay_enabled && (archive_file!=NULL) ) {
	    _DBG_<<"trying to get archived histogram: " << hnamepath << endl;
	    TH1* archived_hist = (TH1*)archive_file->Get(hnamepath.c_str());
	    
	    if(archived_hist) { 
		// only plot the archived histogram if we can find it
		_DBG_<<"found it!"<<endl;
		do_overlay = true;
	
		// only display a copy of the archived histogram so that we can 
		// compare to the original
		if(overlay_hist)
		    delete overlay_hist;
		overlay_hist = (TH1*)(archived_hist->Clone());   // memory leak?

		overlay_hist->SetStats(0);  // we want to compare just the shape of the archived histogram
		overlay_hist->SetFillColor(5); // draw archived histograms with shading

		overlay_ymin = overlay_hist->GetMinimum();
		overlay_ymax = 1.1*overlay_hist->GetMaximum();

		gStyle->SetStatX(0.85);
	    } else {
		gStyle->SetStatX(0.95);
	    }
	    
	    // update histogram with current style parameters
	    // primarily used to update statistics box
	    hist->UseCurrentStyle();
	}
	
	
	// draw summed histogram
	if(!do_overlay) {
	    hist->Draw();

	} else {
	    // set axis ranges so that we can sensibly overlay the histograms
	    double hist_ymax = 1.1*hist->GetMaximum();
	    //double hist_ymax = hist->GetMaximum();
	    double hist_ymin = hist->GetMinimum();
	    // make sure we at least go down to zero, so that we are displaying the whole
	    // distribution - assumes we are just looking at frequency histograms
	    if(hist_ymin > 0)
		hist_ymin = 0;
	    // add in a fix for logarithmic y axes
	    if(display_info.use_logy && hist_ymin == 0)
		hist_ymin = 0.1;
	    
	    hist->GetYaxis()->SetRangeUser(hist_ymin, hist_ymax); 

	    // draw the current histogram with points/error bars if overlaying
	    hist->SetMarkerStyle(20);
	    hist->SetMarkerSize(0.7);
	    
	    // scale down archived histogram and display it to set the scale
	    // appropriately scale the overlay histogram
	    //overlay_hist->Scale(scale);
	    if(display_info.overlay_scale_mode == 1) {   
		// scale both to same overall peak value
		float scale = hist_ymax/overlay_ymax;
		overlay_hist->Scale(scale);
	    } else if(display_info.overlay_scale_mode == 2) {
		// scale to same integral over specified bin range (lo bin # - hi bin #)
		float scale = hist->Integral(display_info.scale_range_low, display_info.scale_range_hi)
		    / overlay_hist->Integral(display_info.scale_range_low, display_info.scale_range_hi);
		overlay_hist->Scale(scale);
	    } else if(display_info.overlay_scale_mode == 3) {
		// scale to same integral over specified bin range (lo % of range - hi % of range)
		int lo_bin = static_cast<int>( (double)(hist->GetNbinsX()) * display_info.scale_range_low/100. );
		int hi_bin = static_cast<int>( (double)(hist->GetNbinsX()) * display_info.scale_range_hi/100. );
		float scale = hist->Integral(lo_bin, hi_bin) / overlay_hist->Integral(lo_bin, hi_bin);
		overlay_hist->Scale(scale);
	    }

	    overlay_hist->Draw();

	    // now print the current histogram on top of it
	    hist->Draw("SAME E1");

	    // add axis to the right for scale of archived histogram
	    if(overlay_yaxis != NULL)
		delete overlay_yaxis;
	    //overlay_yaxis = new TGaxis(gPad->GetUxmax(),hist_ymin,
				       //gPad->GetUxmax(),hist_ymax,
	    overlay_yaxis = new TGaxis(gPad->GetUxmax(),gPad->GetUymin(),
				       gPad->GetUxmax(),gPad->GetUymax(),
				       overlay_ymin,overlay_ymax,510,"+L");
	    overlay_yaxis->SetLabelFont( hist->GetLabelFont() );
	    overlay_yaxis->SetLabelSize( hist->GetLabelSize() );
	    overlay_yaxis->Draw();
	}

    } else if(hdim == hdef_t::twoD) {

	bool do_overlay = false;
	TH1* archived_hist = NULL;
	the_canvas->Clear();
	the_canvas->cd(0);
	if(overlay_enabled && (archive_file!=NULL) ) {
	    _DBG_<<"trying to get archived histogram: " << hnamepath << endl;
	    archived_hist = (TH1*)archive_file->Get(hnamepath.c_str());
	    
	    if(archived_hist) { 
		do_overlay = true;
		the_canvas->Divide(2);
		the_canvas->cd(2);
		archived_hist->Draw("COLZ");
		the_canvas->cd(1);
	    }
	}

	// handle drawing 2D histograms differently
	hist->Draw("COLZ");   // do some sort of pretty printing
	
    } else {
	// default to drawing without arguments
	hist->Draw();
    }


    // finally, after we've finished drawing on the screen, run any macros that may exist
    map<string,string>::iterator macro_iter = macro_files.find(hnamepath);
    if(macro_iter != macro_files.end()) {
      stringstream ss;
      ss << ".x " << macro_iter->second;
      
      _DBG_ << "running macro = " << macro_iter->second << endl;
      
      exec_shell->Exec(ss.str().c_str());
    }
}


void rs_mainframe::DrawMacro(TCanvas *the_canvas, hinfo_t &the_hinfo)
{
	// we're given an hinfo_t which corresponds to just one server
	// so we can go ahead and execute the script we are given

	if(!the_hinfo.macroData) {
		_DBG_ << "Trying to draw a macro with no saved data!" << endl;
		return;
	}

	// extract the macro
	TObjString *macro_str = (TObjString *)the_hinfo.macroData->Get("macro");
	if(!macro_str) {
		_DBG_ << "Could not extract macro from " << the_hinfo.macroData->GetName() << endl;
		return;
	}
	string the_macro( macro_str->GetString().Data() );

	//cerr << "THIS IS THE STRING WE WANT TO RUN: " << endl
	//     << the_macro << endl;

	// move to the right canvas and draw!
	the_canvas->cd();
	ExecuteMacro(the_hinfo.macroData, the_macro);
	the_canvas->Update();
}

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
		for(map<string, hinfo_t>::const_iterator hinfo_itr = the_hdef.hists.begin();
		    hinfo_itr != the_hdef.hists.end(); hinfo_itr++) {
			if( hinfo_itr->second.macroVersion > best_version )
				best_version = hinfo_itr->second.macroVersion;
		}

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

		//cerr << "======= MACRO ==========" << endl;
		//cerr << the_macro << endl;
/*
		// open the summed file
		TFile *f = new TFile(TMP_FILENAME.c_str());
		if(!f) { 
			_DBG_ << "Could not open summed file!" << endl;
			return;
		}
		//f->ls();
		*/		
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

void rs_mainframe::ExecuteMacro(TFile *f, string macro)
{
	TDirectory *savedir = gDirectory;
	f->cd();

	// execute script line-by-line
	// maybe we should do some sort of sanity check first?
	istringstream macro_stream(macro);
	while(!macro_stream.eof()) {
		string s;
		getline(macro_stream, s);
		gROOT->ProcessLine(s.c_str());
	}

	// restore
	savedir->cd();	

#if 0	
	// configuration for file output
	const string base_directory = "/tmp";
	const string base_filename = "RootSpy.macro";   // maybe make this depend on the TMemFile name?

	// write the macro to a temporary file
        //char filename[] = "/tmp/RootSpy.macro.XXXXXX";
	// we have to do a little dance here since the 'XXXXXX' in the filename is
	// replaced by mkstemp() - a better solution should be found
	stringstream ss;
	ss << base_directory << "/" << base_filename << ".XXXXXX";
	char *filename = new char[strlen(ss.str().c_str())+1];
	strcpy(filename, ss.str().c_str());
        int macro_fd = mkstemp(filename);
        if (macro_fd == -1) {
		_DBG_ << "Could not open macro file " << filename << endl;
		delete filename;
		return;
	}
	
	// write the macro to a file
	write(macro_fd, macro.c_str(), macro.length());
	close(macro_fd);

	// execute the macro
	TExec *exec_shell = new TExec();
	string cmd = string(".x ") + filename;
	f->cd();
	exec_shell->Exec(cmd.c_str());

	// restore 
	unlink(filename);
	delete filename;
#endif	

}

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

#if 0
// adapted from TFileMerger::MergeRecursive()
static Bool_t MergeMacroFiles(TDirectory *target, vector<TFile *> &sourcelist)
{
	// Merge all objects in a directory
	Bool_t status = kTRUE;

	if(sourcelist.size() == 0)
		return status;
	
	// Get the dir name
	TString path(target->GetPath());
	// coverity[unchecked_value] 'target' is from a file so GetPath always returns path starting with filename: 
	path.Remove(0, path.Last(':') + 2);

	Int_t nguess = sourcelist.size()+1000;
	THashList allNames(nguess);
	allNames.SetOwner(kTRUE);
	((THashList*)target->GetList())->Rehash(nguess);
	((THashList*)target->GetListOfKeys())->Rehash(nguess);
   
	TFileMergeInfo info(target);

	vector<TFile *>::iterator file_itr = sourcelist.begin();
	TFile      *current_file = *file_itr;
	TDirectory *current_sourcedir = current_file->GetDirectory(path);

	while (current_file || current_sourcedir) {
		// When current_sourcedir != 0 and current_file == 0 we are going over the target
		// for an incremental merge.
		if (current_sourcedir && (current_file == NULL || current_sourcedir != target)) {

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
				
				//if (fNoTrees && cl->InheritsFrom(R__TTree_Class)) {
				//	// Skip the TTree objects and any related cycles.
				//	oldkeyname = key->GetName();
				//	continue;
				//}
            
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
					//TFile *nextsource = current_file ? (TFile*)sourcelist->After( current_file ) : (TFile*)sourcelist->First();
					TFile *nextsource = NULL;
					if(current_file) {
						nextsource = *(++file_itr);
					} else {
						nextsource = sourcelist.front();
					}
					//if (nextsource == 0) {
					if (sourcelist.size() == 1) {
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
										//nextsource = (TFile*)sourcelist->After(nextsource);
										nextsource = *(++file_itr);
										continue;
									}
									// Set ownership for collections
									if (hobj->InheritsFrom(TCollection::Class())) {
										((TCollection*)hobj)->SetOwner();
									}
									hobj->ResetBit(kMustCleanup);
									inputs.Add(hobj);
									//if (!oneGo) {
									ROOT::MergeFunc_t func = obj->IsA()->GetMerge();
									Long64_t result = func(obj, &inputs, &info);
									info.fIsFirst = kFALSE;
									if (result < 0) {
										Error("MergeRecursive", "calling Merge() on '%s' with the corresponding object in '%s'",
										      obj->GetName(), nextsource->GetName());
									}
									inputs.Delete();
									//}
								}
							}
							//nextsource = (TFile*)sourcelist->After( nextsource );
							nextsource = *(++file_itr);
						} while (nextsource);
						// Merge the list, if still to be done
						//if (oneGo || info.fIsFirst) {
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
					//TFile *nextsource = current_file ? (TFile*)sourcelist->After( current_file ) : (TFile*)sourcelist->First();
					TFile *nextsource = NULL;
					if(current_file) {
						nextsource = *(++file_itr);
					} else {
						nextsource = sourcelist.front();
					}
					//if (nextsource == 0) {
					if (sourcelist.size() == 1) {
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
										//nextsource = (TFile*)sourcelist->After(nextsource);
										nextsource = *(++file_itr);
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
							//nextsource = (TFile*)sourcelist->After( nextsource );
							nextsource = *(++file_itr);
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
					//TFile *nextsource = current_file ? (TFile*)sourcelist->After( current_file ) : (TFile*)sourcelist->First();
					TFile *nextsource = NULL;
					if(current_file) {
						nextsource = *(++file_itr);
					} else {
						nextsource = sourcelist.front();
					}
					//if (nextsource == 0) {
					if (sourcelist.size() == 1) {
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
										//Info("MergeRecursive", "could not read object for key {%s, %s}; skipping file %s",
										//   key->GetName(), key->GetTitle(), nextsource->GetName());
										//nextsource = (TFile*)sourcelist->After(nextsource);
										nextsource = *(++file_itr);
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
							//nextsource = (TFile*)sourcelist->After( nextsource );
							nextsource = *(++file_itr);
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
		//current_file = current_file ? (TFile*)sourcelist->After(current_file) : (TFile*)sourcelist->First();
		if(++file_itr == sourcelist.end()) {
			current_sourcedir = NULL;
		} else {
			current_file = *file_itr;
			current_sourcedir = current_file->GetDirectory(path);
		}
	} 
	// save modifications to the target directory.
	target->SaveSelf(kTRUE);

	return status;
}
#endif
