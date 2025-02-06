// $Id$
//
//    File: DRootSpy.h
// Created: Thu Aug 27 13:40:02 EDT 2009
// Creator: davidl (on Darwin harriet.jlab.org 9.8.0 i386)
//
 	
#if HAVE_CONFIG_H
#include <rootspy_config.h>
#endif

#ifndef _DRootSpy_
#define _DRootSpy_


#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/time.h>


#include <vector>
#include <set>
#include <string>
#include <iostream>

#include "macro_info_t.h"
#include <rs_global_root_lock.h>

#include <TFile.h>
#include <TDirectory.h>
#include <TH1.h>
#include <pthread.h>

#include <cMsg.hxx>
using namespace cmsg;


#include <memory> 
#include <xmsg/util.h>
#include <xmsg/xmsg.h>
using namespace xmsg;

#ifndef _DBG_
#define _DBG_ std::cerr<<__FILE__<<":"<<__LINE__<<" "
#define _DBG__ std::cerr<<__FILE__<<":"<<__LINE__<<std::endl
#endif

extern RSLock *gROOTSPY_RW_LOCK;
extern string gROOTSPY_PROGRAM_NAME;


class DRootSpy : public cMsgCallback {
public:

		typedef map<string, const xmsg::proto::Data*> RSPayloadMap;

		DRootSpy(                                string udl            , string myname="");
		DRootSpy(RSLock           *rw_lock=NULL, string udl="<default>", string myname="");
		DRootSpy(pthread_rwlock_t *rw_lock=NULL, string udl="<default>", string myname="");
		void Initialize(RSLock *rw_lock, string udl);
		virtual ~DRootSpy();

		void ConnectToCMSG(void);
		void ConnectToXMSG(void);
		void* WatchConnection(void);
		void* DebugSampler(void);

		void AddNameFilter(string s){ filter_patterns.insert(s); }
		set<string>& GetNameFilters(void){ return filter_patterns; }
		void PrintNameFilters(void){
			std::cout <<"-- ROOTSpy Name Filter Patterns --" << std::endl;
			for(auto s : filter_patterns) std::cout << "  " << s << std::endl;
			std::cout <<"----------------------------------" << std::endl;
		}

		class hinfo_t{
			public:
				string name;
				string title;
				string type;
				string path;
		};
		class tree_info_t {
			public:
				string name;
				string title;
				string path;
				vector<string> branch_info;
		};
		void ReturnFinals(void);

		// macro functions
		bool RegisterMacro(string name, string path, string macro_data);
		bool AddMacroHistogram(string name, string path, TH1 *the_hist);
		bool AddMacroHistogram(string name, string path, vector<TH1*> the_hists);
		bool SetMacroVersion(string name, string path, int version_number);
		bool SetSumHistsOnly(bool advertise_sum_hists_only=true);
		void GetMacroInfos( map<string,macro_info_t> &mymacros ){ mymacros = macros;}
		void PopRequested_hnamepaths(set<string> &hnamepaths);

		static double GetTime(void){
			struct timeval tval;
			struct timezone tzone;
			gettimeofday(&tval, &tzone);
			double t = (double)tval.tv_sec+(double)tval.tv_usec/1.0E6;
			if(start_time==0.0) start_time = t;
			return t - start_time;
		}
		static double start_time;

		map<string, map<string, double> > last_sent; // outer key=server, inner key=hnamepath

		int VERBOSE;
		bool DEBUG;

		bool in_callback;
		bool in_list_hists;
		bool in_list_macros;
		bool in_get_hist;
		bool in_get_hists;
		bool in_get_tree;
		bool in_get_tree_info;
		bool in_get_macro;

		enum{

			kNSAMPLES,
			kREADLOCKED,
			kWRITELOCKED,

			kINCALLBACK,
			kIN_LIST_HISTS,
			kIN_LIST_MACROS,
			kIN_GET_HIST,
			kIN_GET_HISTS,
			kIN_GET_TREE,
			kIN_GET_TREE_INFO,
			kIN_GET_MACRO,

			kNCOUNTERS
		}RSBinDefs_t;

		TFile *debug_file;
		TH1I *hcounts;
		TH1D *hfractions;

		void callback(xmsg::Message &msg); // callback for xMsg
		void callback(cMsgMessage *msg, void *userObject);
		void addRootObjectsToList(TDirectory *dir, vector<hinfo_t> &hinfos);
		void addTreeObjectsToList(TDirectory *dir, vector<tree_info_t> &tree_infos);
		void findTreeObjForMsg(TDirectory *dir, string sender);
		void findTreeNamesForMsg(TDirectory *dir, vector<string> &tree_names,
		vector<string> &tree_titles, vector<string> &tree_paths);     

		template<typename V>
		void SendMessage(string servername, string command, V&& data, string dataType="mystery");
		void SendMessage(string servername, string commands);
		void SendMessage(string servername, string command, xmsg::proto::Payload &payload);

		template<typename V>
		void AddToPayload(xmsg::proto::Payload &payload, string name, V &data);

	protected:
		//class variables
		cMsg *cMsgSys;
		cMsgSubscriptionConfig *cMsgSubConfig;
		xmsg::xMsg *xmsgp;
		xmsg::ProxyConnection *pub_con;
		bool own_gROOTSPY_RW_LOCK = false;
		TDirectory *hist_dir; // save value of gDirectory used when forming response to "list hist" request
		string myname;
		string myudl;
		std::vector<void*> cmsg_subscription_handles;
		std::vector< xmsg::Subscription* > xmsg_subscription_handles;
		vector<string> *finalhists;
		set<string> filter_patterns;
		pthread_t mythread;
		pthread_t mywatcherthread;
		pthread_t mydebugthread;
		string finalsender;
		bool done;
		bool advertise_sum_hists_only; // Only send histograms in our sum list (used by RSAggregator)
		set<string> requested_hnamepaths;
		mutex pub_mutex;

		map<string,macro_info_t> macros;

		//methods
		void traverseTree(TObjArray *branch_list, vector<string> &treeinfo);
		void listHists(cMsgMessage &response);
		void listHists(string sender);
		void getHist(cMsgMessage &response, string &hnamepath, bool send_message=true);
		void getHist(string &sender, string &hnamepath, bool send_message=true);
		void getHistUDP(void *response, string hnamepath, uint32_t addr32, uint16_t port, uint32_t tcpaddr32, uint16_t tcpport);
		void getHistTCP(void *response, string hnamepath, uint32_t addr32, uint16_t port);
		void getHists(cMsgMessage &response, vector<string> &hnamepaths);
		void getHists(string sender, vector<string> &hnamepaths);
		void getTree(cMsgMessage &response, string &name, string &path, int64_t nentries);
		void treeInfo(string sender);
		void treeInfoSync(cMsgMessage &response, string sender);
		void listMacros(cMsgMessage &response);
		void listMacros(string sender);
		void getMacro(cMsgMessage &response, string &hnamepath);
		void getMacro(string sender, string &hnamepath, bool include_histos);

};

extern DRootSpy *gROOTSPY;


//---------------------------------
// SendMessage
//
// Send an xMsg message with the specified command and data 
// to the specified server. The value of data must be passed
// as an r-value (wrap it in std:move()). The servername
// should be the unique name of the process (i.e. the subject
// to which the message is addressed)
//
//  The "command" value will be passed as the description field 
// of the xMsg Meta and will also be used as the "Type" in
// the Topic the message is addressed to. The "sender", "replyTo",
// and "executionTime" fields of the message will all be
// automatically filled in before the message is send.
//
//  The datatype field will usually be either "none" or
// "xmsg::proto::Payload" set by one of the two non-templated
// SendMessage methods. If nothing is passed for the dataType
// string, then it defaults to "mystery" which just seems
// slightly more fun than "unknown".
//
// n.b. the message will only be sent if is_online is true.
//---------------------------------
template<typename V>
void DRootSpy::SendMessage(string servername, string command, V&& data, string dataType)
{
	// Topic to address the message to
	auto topic = xmsg::Topic::build("rootspy", servername, command);

	// Meta data of xMsg
	auto MyMeta = xmsg::proto::make_meta();
	MyMeta->set_description(command);
	MyMeta->set_replyto(myname);
	MyMeta->set_sender(myname);
	MyMeta->set_datatype(dataType);
	MyMeta->set_executiontime( time(NULL) );

	// Create message and send it
	xmsg::Message msg( std::move(topic), std::move(MyMeta), std::move(data));
	if(VERBOSE>3) _DBG_ << "Sending \"" << command << "\"" << endl;
	lock_guard<mutex> lck(pub_mutex);
	xmsgp->publish(*pub_con, msg);
}

//---------------------------------
// AddToPayload
//
// Add an item to the given Payload object
//---------------------------------
template<typename V>
void DRootSpy::AddToPayload(xmsg::proto::Payload &payload, string name, V &data)
{
	auto item = payload.add_item();
	item->set_name(name);
	auto mydata = xmsg::proto::make_data(data);
	item->mutable_data()->CopyFrom( mydata );
}


#endif // _DRootSpy_

