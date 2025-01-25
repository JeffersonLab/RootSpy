
#ifndef _RS_MAINFRAME_H_
#define _RS_MAINFRAME_H_


// This class is made into a ROOT dictionary ala rootcint.
// Therefore, do NOT include anything Hall-D specific here.
// It is OK to do that in the .cc file, just not here in the 
// header.

#include <iostream>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <time.h>

#include <TGClient.h>
#include <TGButton.h>
#include <TCanvas.h>
#include <TText.h>
#include <TRootEmbeddedCanvas.h>
#include <TTUBE.h>
#include <TNode.h>
#include <TGComboBox.h>
#include <TPolyLine.h>
#include <TEllipse.h>
#include <TMarker.h>
#include <TVector3.h>
#include <TGLabel.h>
#include <TTimer.h>
#include <TH1.h>
#include <TFile.h>
#include <TGMenu.h>
#include <TGDockableFrame.h>
#include <TExec.h>
#include <TMemFile.h>
#include <TDirectory.h>
#include <TGTab.h>
#include <TLatex.h>
#include <TVirtualX.h>
#include <TObjString.h>


#include "RSTab.h"
#include "Dialog_SelectHists.h"
#include "Dialog_SaveHists.h"
#include "Dialog_ReferencePlot.h"
#include "Dialog_NewReference.h"
#include "../libRootSpy-client/hinfo_t.h"
#include "../libRootSpy-client/hdef_t.h"

class rs_mainframe:public TGMainFrame {

	public:
		enum viewStyle_t_rs{
			kViewByObject_rs,
			kViewByServer_rs
		};
		
#if !defined(__CINT__) && !defined(__CLING__)

		typedef struct {
				string type;
				vector<string> tokens;
				vector<string> subitems;
		}config_item_t;

		typedef struct {
				string name;
				int currently_displayed;
				vector<string> hnamepaths;
		}tab_config_t;

		bool TokenizeFile(string fname, map<string, vector<config_item_t> > &config_items);
#endif //__CINT__  __CLING__



		rs_mainframe(const TGWindow *p, UInt_t w, UInt_t h, bool build_gui, string startup_config_filename);
		~rs_mainframe();

		void SavePreferences(void);
		void ReadPreferences(void);
		void SaveConfigAs(void);
		void SaveConfig(void);
		void ReadConfig(string fname="");
		
		// Virtual methods and menu handler
		void   CloseWindow(void);
		Bool_t HandleKey(Event_t *event);
		void   HandleMenu(Int_t id);
		
		// Slots for ROOT GUI
		void DoQuit(void);
		void DoMakeTB(void);
		void DoSetWindowSize(void);		
		void DoResetDialog(void);
		void DoSetScaleOptions(void);
		void DoNewTabDialog(void);
		void DoRemoveTabDialog(void);
		void DoTabSelected(Int_t id);
		void DoSaveHists(void);
		void DoTreeInfo(void);
		void DoConfigMacros(void);
		void DoSelectDelay(Int_t index);
		void DoTimer(void);
		void DoSetArchiveFile(void);
		void DoTreeInfoShort(void);
		void DoSetViewOptions(int menu_item);
		void DoUpdateViewMenu(void);
		void DoLoadHistsList(void);
		void DoSaveHistsList(void);
		void DoFinal(void);
		void DoELog(void);
		void ELogEntryThread(void);
		void DoHDMonGUI(void);
		void DoReferencePlot(void);
		void DoMakeReferencePlot(void);
				
		//void QueueMacroForRegistration(string name. string path, int version, 

		// Helper methods for building GUI
		TGLabel*          AddLabel(TGCompositeFrame* frame, string text, Int_t mode=kTextLeft, ULong_t hints=kLHintsLeft | kLHintsTop);
		TGTextButton*     AddButton(TGCompositeFrame* frame, string text, ULong_t hints=kLHintsLeft | kLHintsTop);
		TGCheckButton*    AddCheckButton(TGCompositeFrame* frame, string text, ULong_t hints=kLHintsLeft | kLHintsTop);
		TGPictureButton*  AddPictureButton(TGCompositeFrame* frame, string picture, string tooltip="", ULong_t hints=kLHintsLeft | kLHintsTop);
		TGFrame*          AddSpacer(TGCompositeFrame* frame, UInt_t w=10, UInt_t h=10, ULong_t hints=kLHintsCenterX | kLHintsCenterY);


		TGTab *fMainTab;
		TGLabel *lUDL;
		TGLabel *lArchiveFile;
		TGCheckButton *bAutoRefresh;
		TGCheckButton *bAutoAdvance;
		TGComboBox *fDelay;
		TGCheckButton *bShowOverlays;
		TLatex *run_number_label;
		
		TGMainFrame *dialog_savehists;
		TGMainFrame *dialog_selecttree;
		TGMainFrame *dialog_configmacros;
		TGMainFrame *dialog_scaleopts;
		TGMainFrame *dialog_referenceplot;
		TGMainFrame *dialog_newreference;

		TGDockableFrame    *fMenuDock;
		TGMenuBar  *fMenuBar;
		TGPopupMenu  *fMenuFile, *fMenuTools, *fMenuView;
		TGLayoutHints      *fMenuBarLayout, *fMenuBarItemLayout, *fMenuBarHelpLayout;

		bool delete_dialog_selecttree;
		bool delete_dialog_savehists;
		bool delete_dialog_configmacros;
		bool delete_dialog_scaleopts;
		
		string config_filename;
		map<string,string> macro_files;
		list<RSTab*> rstabs;
		RSTab *current_tab;
		void GetAllDisplayedHNamepaths(set<string> &hnamepaths);

		viewStyle_t_rs viewStyle_rs;
		void ExecuteMacro(TDirectory* f, string macro);
		void DrawMacro(TCanvas*the_canvas, hinfo_t &the_hinfo);
		void DrawMacro(TCanvas*the_canvas, hdef_t &the_hdef);
		void DropAllHists(void);

		// info for comparing with archived histograms
		//bool overlay_mode; // maybe don't need this?
		TFile *archive_file;
		
		long epics_run_number;
		double last_epics_run_number_checked; // last time we tried getting run number from EPICS

	private:
	
		TTimer *timer;
		long sleep_time; // in milliseconds
		double last_called;				// last time DoTimer() was called
		double last_ping_time;			// last time we broadcast for a server list
		double last_hist_requested;	    // last time we requested list of histograms
		
		TGRadioButton *VBServer;

		time_t delay_time;
		
		hid_t last_requested;	// last hnamepath requested
		TH1 *last_hist_plotted;
		int selected_tab_from_prefrences;
		TGDimension winsize_from_preferences;
		
		TTimer *elog_timer;
		unsigned int Npages_elog;
		unsigned int ipage_elog;
		double elog_next_action;
		int elog_tab_restore;
		int elog_plot_restore;
	
		void CreateGUI(void);

		TExec *exec_shell;

		
	ClassDef(rs_mainframe,1)
};



#endif //_RS_MAINFRAME_H_
