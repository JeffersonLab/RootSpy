// $Id$
//
//    File: rs_xmsg.cc
// Created: Thu Aug 27 13:40:02 EDT 2009
// Creator: davidl (on Darwin harriet.jlab.org 9.8.0 i386)
//

#include <unistd.h>
#include <fcntl.h>

#include "RootSpy.h"
#include "rs_xmsg.h"
#include "rs_info.h"
#include "tree_info_t.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <set>
#include <mutex>
using namespace std;

#include <TDirectoryFile.h>
#include <TMessage.h>
#include <TH1.h>
#include <TTree.h>
#include <TMemFile.h>
#include <TKey.h>
#include <THashList.h>
#include <TObjString.h>



mutex REGISTRATION_MUTEX_XMSG;
set<rs_serialized*> HISTOS_TO_REGISTER_XMSG;
set<rs_serialized*> MACROS_TO_REGISTER_XMSG;


double rs_xmsg::start_time=0.0; // overwritten on first call to rs_xmsg::GetTime()
rs_xmsg* rs_xmsg::_rs_xmsg_global=NULL; // Overwritten in rs_cmsg constructor

// The following is a class just to wrap the call to rs_xmsg::callback
// This is needed because the xMsg::subscribe method will create and destroy
// multiple instances of this callback object. Using an operator() method
// in rs_xmsg itself thus becomes problematic.
class LocalCallBack{
	public:
		void operator()(xmsg::Message &msg){ rs_xmsg::GetGlobalPtr()->callback( msg ); }
};


//---------------------------------
// rs_xmsg    (Constructor)
//---------------------------------
rs_xmsg::rs_xmsg(string &udl, string &name, bool connect_to_xmsg):pub_con(NULL)
{
	_rs_xmsg_global = this; // needed by LocalCallBack via GetGlobalPtr

	verbose = 1; // higher values=more messages. 0=minimal messages
	hist_default_active = true;
	program_name = "rootspy-client"; // should be overwritten by specific program

	udpdev         = NULL;
	udpport        = 0;
	udpthread      = NULL;
	stop_udpthread = false;

	tcpdev         = NULL;
	tcpport        = 0;
	tcpthread      = NULL;
	stop_tcpthread = false;
	
	// Confirm this is an xmsg udl
	if( udl.find("xMsg://") != 0 ){
		is_online = false;
		return;
	}
	
	// Extract name of proxy to connect to
	string proxy_host = udl.length()>7 ? udl.substr(7):"localhost";
	auto bind_to = xmsg::util::to_host_addr(proxy_host);

	// Connect to xmsg system
	is_online = false;
	if(connect_to_xmsg){

		try {

			xmsgp = new xMsg(name);
			ProxyAddress pa(bind_to);
		    xmsgp->connect(pa); 

			// Create a unique name for ourself
			char hostname[256];
			gethostname(hostname, 256);
			char str[512];
			snprintf(str, 512, "rs_%s_%d", hostname, getpid());
			myname = string(str);

			cout<<"---------------------------------------------------"<<endl;
			cout<<"   xMsg name: \""<<name<<"\""<<endl;
			cout<<"rootspy name: \""<<myname<<"\""<<endl;
			cout<<"---------------------------------------------------"<<endl;

			// Subscribe to generic rootspy info requests
			
			auto connection = xmsgp->connect(pa);
			auto topic_all = xmsg::Topic::build("rootspy", "rootspy", "*");
			auto cb = LocalCallBack{};

			subscription_handles.push_back( xmsgp->subscribe(topic_all, std::move(connection), cb).release() );

			// Subscribe to rootspy requests specific to me
			connection = xmsgp->connect(pa); // xMsg requires unique connections
			auto topic_me = xmsg::Topic::build("rootspy", myname, "*");
			subscription_handles.push_back( xmsgp->subscribe(topic_me, std::move(connection), cb).release() );

			// Create connection for outgoing messages
			pub_con = new xmsg::ProxyConnection( xmsgp->connect(pa) );

			// Broadcast request for available servers
			PingServers();

			is_online = true;
		} catch ( std::exception& e ) {
			cout<<endl<<endl<<endl<<endl<<"_______________  ROOTSPY unable to connect to xmsg system! __________________"<<endl;
			cout << e.what() << endl; 
			cout<<endl<<endl<<endl<<endl;

			// we need to connect to xmsg to run
			is_online = false;
			exit(0);          
		}

	}else{
		cout<<"---------------------------------------------------"<<endl;
		cout<<"  Not connected to xmsg system. Only local histos  "<<endl;
		cout<<"  and macros will be available.                    "<<endl;
		cout<<"---------------------------------------------------"<<endl;	
	}

}

//---------------------------------
// ~rs_xmsg    (Destructor)
//---------------------------------
rs_xmsg::~rs_xmsg()
{
	// Unsubscribe
	// (n.b. can't use "auto" here since that would require a copy of the unique_ptr
	// which causes a compiler error)
	for( auto sub : subscription_handles ){
		xmsgp->unsubscribe( std::unique_ptr<xmsg::Subscription>( sub ) );
	}
	
	if( pub_con ) delete pub_con;
	
	delete xmsgp;
		
	if(udpthread){
		stop_udpthread = true;
		udpthread->join();
	}

	if(tcpthread){
		stop_tcpthread = true;
		tcpthread->join();
	}
	
	// Optionally write out stats to ROOT file
	const char *ROOTSPY_DEBUG = getenv("ROOTSPY_DEBUG");
	if(ROOTSPY_DEBUG!=NULL){
		string fname = strlen(ROOTSPY_DEBUG)>0 ? ROOTSPY_DEBUG:"rsclient_stats_xmsg.root";

		// Grab ROOT lock and open output file
		pthread_rwlock_wrlock(ROOT_MUTEX);
		TDirectory *savedir = gDirectory;
		TFile *debug_file = new TFile(fname.c_str(), "RECREATE");
		cout << "--- Writing ROOTSPY_DEBUG to: " << fname << " ----" << endl;

		// Make complete list of all requested and received 
		// hnamepaths. This will allow us to make the x-axis
		// bins of both the requested and received identical
		_DBG_<<"requested_histograms,size()="<<requested_histograms.size()<<endl;
		_DBG_<<"received_histograms,size()="<<received_histograms.size()<<endl;
		set<string> hnamepaths;
		map<string, uint32_t>::iterator iter;
		for(iter=requested_histograms.begin(); iter!=requested_histograms.end(); iter++){
			hnamepaths.insert(iter->first);
			_DBG_<<iter->first<<endl;
		}
		for(iter=received_histograms.begin(); iter!=received_histograms.end(); iter++){
			hnamepaths.insert(iter->first);
			_DBG_<<iter->first<<endl;
		}
		
		if(hnamepaths.empty()) hnamepaths.insert("no_histograms");

		// Requested/received hists
		TH1I * hreqhists = new TH1I("requestedhists", "Requested histograms", hnamepaths.size(), 0, hnamepaths.size());
		TH1I * hrcdhists = new TH1I("receivedhists", "Received histograms", hnamepaths.size(), 0, hnamepaths.size());
		set<string>::iterator siter=hnamepaths.begin();
		for(int ibin=1; siter!=hnamepaths.end(); siter++, ibin++){
			string hnamepath = *siter;
			hreqhists->GetXaxis()->SetBinLabel(ibin, hnamepath.c_str());
			hrcdhists->GetXaxis()->SetBinLabel(ibin, hnamepath.c_str());
			
			hreqhists->SetBinContent(ibin, requested_histograms[hnamepath]);
			hrcdhists->SetBinContent(ibin, received_histograms[hnamepath]);
		}
		
		// Close ROOT file
		debug_file->Write();
		debug_file->Close();
		delete debug_file;

		// Restore ROOT directory and release lock
		savedir->cd();
		pthread_rwlock_unlock(ROOT_MUTEX);

		cout << "-------------- Done --------------" << endl;
	}
}

//---------------------------------
// SendMessage
//
// This is a wrapper for the templated version of this method
// defined in the header file. This is useful for commands that
// do not require an associated payload. Since xmsg requires
// a non-NULL payload, we just create an empty one for it.
//
// See comments for templated SendMessage method in header for
// details.
//---------------------------------
void rs_xmsg::SendMessage(string servername, string command)
{
	std::vector<std::uint8_t> buffer;
	SendMessage( servername, command, std::move(buffer), "none" );
}

//---------------------------------
// SendMessage
//
// This is another wrapper for the templated version of this method
// defined in the header file. This is useful for commands that
// DO require an associated payload, but have it in the form of
// and xMsg Payload object. This will serialize the payload object
// and pass it into the templated mathod
//
// See comments for templated SendMessage method in header for
// details.
//---------------------------------
void rs_xmsg::SendMessage(string servername, string command, xmsg::proto::Payload &payload)
{
	auto buffer = std::vector<std::uint8_t>(payload.ByteSizeLong());
	payload.SerializeToArray( buffer.data(), buffer.size() );
	SendMessage( servername, command, std::move(buffer), "xmsg::proto::Payload" );
}


//////////////////////////////////////
//// helper functions
//////////////////////////////////////
#if 0
//---------------------------------
// BuildRequestHists
//---------------------------------
void rs_xmsg::BuildRequestHists(xmsg::Message &msg, string servername)
{
    msg.setSubject(servername);
    msg.setType(myname);
    msg.setText("list hists");
	 uint64_t tsent = (uint64_t)time(NULL);
	 msg.add("time_sent", tsent);
}

//---------------------------------
// BuildRequestHistogram
//---------------------------------
void rs_xmsg::BuildRequestHistogram(xmsg::Message &msg, string servername, string hnamepath)
{
    msg.setSubject(servername);
    msg.setType(myname);
    msg.setText("get hist");
	 uint64_t tsent = (uint64_t)time(NULL);
	 msg.add("time_sent", tsent);
    msg.add("hnamepath", hnamepath);
	 if(udpdev){
    	msg.add("udpaddr", udpdev->addr32);
    	msg.add("udpport", udpport);
	}
	 if(tcpdev){
    	msg.add("tcpaddr", tcpdev->addr32);
    	msg.add("tcpport", tcpport);
	}
}

//---------------------------------
// BuildRequestHistograms
//---------------------------------
void rs_xmsg::BuildRequestHistograms(xmsg::Message &msg, string servername, vector<string> &hnamepaths)
{
    msg.setSubject(servername);
    msg.setType(myname);
    msg.setText("get hists");
	 uint64_t tsent = (uint64_t)time(NULL);
	 msg.add("time_sent", tsent);
    msg.add("hnamepaths", hnamepaths);
}

//---------------------------------
// BuildRequestTreeInfo
//---------------------------------
void rs_xmsg::BuildRequestTreeInfo(xmsg::Message &msg, string servername)
{
    msg.setSubject(servername);
    msg.setType(myname);
	 uint64_t tsent = (uint64_t)time(NULL);
	 msg.add("time_sent", tsent);
    msg.setText("get tree info");
}

//---------------------------------
// BuildRequestTree
//---------------------------------
void rs_xmsg::BuildRequestTree(xmsg::Message &msg, string servername, string tree_name, string tree_path, int64_t num_entries)
{
    msg.setSubject(servername);
    msg.setType(myname);
    msg.setText("get tree");
	 uint64_t tsent = (uint64_t)time(NULL);
	 msg.add("time_sent", tsent);
    msg.add("tree_name", tree_name);
    msg.add("tree_path", tree_path);
    msg.add("num_entries", num_entries);
}

//---------------------------------
// BuildRequestMacroList
//---------------------------------
void rs_xmsg::BuildRequestMacroList(xmsg::Message &msg, string servername)
{
    msg.setSubject(servername);
    msg.setType(myname);
	 uint64_t tsent = (uint64_t)time(NULL);
	 msg.add("time_sent", tsent);
    msg.setText("list macros");
}

//---------------------------------
// BuildRequestMacro
//---------------------------------
void rs_xmsg::BuildRequestMacro(xmsg::Message &msg, string servername, string hnamepath)
{
    msg.setSubject(servername);
    msg.setType(myname);
    msg.setText("get macro");
	 uint64_t tsent = (uint64_t)time(NULL);
	 msg.add("time_sent", tsent);
    msg.add("hnamepath", hnamepath);
	 if(verbose>4) _DBG_ << "preparing to request macro with hnamepath=\"" << hnamepath << "\"" << endl;
}
#endif

//////////////////////////////////////
//// remote commands
//////////////////////////////////////

//---------------------------------
// PingServers
//---------------------------------
void rs_xmsg::PingServers(void)
{
	SendMessage("rootspy", "who's there?");
}

//---------------------------------
// RequestHists
//---------------------------------
void rs_xmsg::RequestHists(string servername)
{
	SendMessage(servername, "list hists");
}

//---------------------------------
// RequestHistogram
//---------------------------------
void rs_xmsg::RequestHistogram(string servername, string hnamepath)
{
	xmsg::proto::Payload payload;
	AddToPayload(payload, "hnamepath", hnamepath);
	if(udpdev){
		::google::protobuf::int64 udpaddr = udpdev->addr32;
    	AddToPayload(payload, "udpaddr", udpaddr);
    	AddToPayload(payload, "udpport", udpport);
	}
	 if(tcpdev){
		::google::protobuf::int64 tcpaddr = tcpdev->addr32;
    	AddToPayload(payload, "tcpaddr", tcpaddr);
    	AddToPayload(payload, "tcpport", tcpport);
	}
	SendMessage(servername, "get hist", payload);

	if(is_online) requested_histograms[hnamepath]++;
}

//---------------------------------
// RequestHistograms
//---------------------------------
void rs_xmsg::RequestHistograms(string servername, vector<string> &hnamepaths)
{
	xmsg::proto::Payload payload;
	AddToPayload(payload, "hnamepaths", hnamepaths);
	if(udpdev){
		::google::protobuf::int64 udpaddr = udpdev->addr32;
    	AddToPayload(payload, "udpaddr", udpaddr);
    	AddToPayload(payload, "udpport", udpport);
	}
	 if(tcpdev){
		::google::protobuf::int64 tcpaddr = tcpdev->addr32;
    	AddToPayload(payload, "tcpaddr", tcpaddr);
    	AddToPayload(payload, "tcpport", tcpport);
	}
	SendMessage(servername, "get hists", payload);

	if(is_online) for(auto h : hnamepaths) requested_histograms[h]++;
}

#if 0
//---------------------------------
// RequestTreeInfo
//---------------------------------
void rs_xmsg::RequestTreeInfo(string servername)
{
	xmsgMessage treeinfo;
	BuildRequestTreeInfo(treeinfo, servername);
	if(is_online){
		if(verbose>3) _DBG_ << "Sending \"" << treeinfo.getText() << "\"" << endl;
		xmsgSys->send(&treeinfo);
	}
}

//---------------------------------
// RequestTree
//---------------------------------
void rs_xmsg::RequestTree(string servername, string tree_name, string tree_path, int64_t num_entries = -1)
{
	xmsgMessage requestTree;
	BuildRequestTree(requestTree, servername, tree_name, tree_path, num_entries);
	if(is_online){
		if(verbose>3) _DBG_ << "Sending \"" << requestTree.getText() << endl;
		xmsgSys->send(&requestTree);
	}
}
#endif

//---------------------------------
// RequestMacroList
//---------------------------------
void rs_xmsg::RequestMacroList(string servername)
{
	SendMessage(servername, "list macros");
}

//---------------------------------
// RequestMacro
//---------------------------------
void rs_xmsg::RequestMacro(string servername, string hnamepath, bool include_histos)
{
	string include_histos_str = include_histos ? "true":"false";
	xmsg::proto::Payload payload;
	AddToPayload(payload, "hnamepath", hnamepath);
	AddToPayload(payload, "include_histos", include_histos_str);
	SendMessage(servername, "get macro", payload);

	if(is_online) requested_macros[hnamepath]++;
}

//---------------------------------
// FinalHistogram
//---------------------------------
void rs_xmsg::FinalHistogram(string servername, vector<string> hnamepaths)
{
	xmsg::proto::Payload payload;
	AddToPayload(payload, "hnamepaths", hnamepaths);
	SendMessage(servername, "provide final", payload);
}


//---------------------------------
// callback
//---------------------------------
void rs_xmsg::callback(xmsg::Message &msg)
{	
	// The convention here is that the message "type" always contains the
	// unique name of the sender and therefore should be the "subject" to
	// which any reponse should be sent.
	string sender = msg.meta()->replyto();
	if(sender == myname){  // no need to process messages we sent!
		if(verbose>4) cout << "Ignoring message from ourself (\""<<sender<<"\")" << endl;
		return;
	}
	
	// If the message has a Payload object serialized in the payload,
	// then deserialize it and make a map of the items
	RSPayloadMap payload_items;
	xmsg::proto::Payload payload; // n.b. needs to maintain same scope as above container!
	if( msg.meta()->datatype() == "xmsg::proto::Payload" ){
		
		auto &buffer = msg.data();
		payload.ParseFromArray( buffer.data(), buffer.size() );
			
		for(int i=0; i<payload.item_size(); i++ ){
			payload_items[payload.item(i).name()] = &payload.item(i).data();
		}
	}

	// Optional Debugging messages
	if(verbose>6){
	
		_DBG_ << "xmsg recieved --" <<endl;
		_DBG_ << "    command: " << msg.meta()->description() << endl;
		_DBG_ << "   datatype: " << msg.meta()->datatype() << endl;
		for( auto p : payload_items ) _DBG_ << "       item: " << p.first << endl;
	}

#if 0
	if(verbose>2){
		// print round trip time
		try{
			uint64_t t_originated = msg->getUint64("time_requester");
			uint64_t t_now = (uint64_t)time(NULL);
			_DBG_ << " " << msg->getText() << " : response time from "<< sender << " = " << (t_now-t_originated) << "secs" << endl;
			if(verbose>4){
				uint64_t t_responded = msg->getUint64("time_sent");
				uint64_t t_received = msg->getUint64("time_received");
				_DBG_ << "  t_originated = " << t_originated << endl; 
				_DBG_ << "    t_received = " << t_received << endl; 
				_DBG_ << "   t_responded = " << t_responded << endl; 
				_DBG_ << "         t_now = " << t_now << endl; 
			}
		}catch(...){}
	}

	if(verbose>4){
		// print round trip time
		try{
			vector<int32_t> *queue_counts = msg->getInt32Vector("queue_counts");
			for(uint32_t i=0; i<queue_counts->size() ;i++){
				_DBG_ << "  queue_counts["<<i<<"] = " << (*queue_counts)[i] << endl;
			}
		}catch(...){}
	}
#endif

	// Look for an entry for this server in RS_INFO map.
	// If it is not there then add it.
	RS_INFO->Lock();
	
	if(RS_INFO->servers.find(sender)==RS_INFO->servers.end()){
		RS_INFO->servers[sender] = server_info_t(sender);
		if(verbose>=2) cout<<"Added \""<<sender<<"\" to server list."<<endl;
	}else{
		// Update lastHeardFrom time for this server
		RS_INFO->servers[sender].lastHeardFrom = time(NULL);
		if(verbose>=4) _DBG_ <<"Updated \""<<sender<<"\" lastHeardFrom."<<endl;
	}
	RS_INFO->Unlock();

	// The actual command is always sent in the description of the message
	string cmd = msg.meta()->description();
	if(verbose>3) _DBG_ << "msg.meta()->description() = \"" << cmd << "\"" << endl;
	if (cmd == "null") return;

	// Dispatch command
	bool handled_message = false;
	
	//===========================================================
	if(cmd=="who's there?"){
		xmsg::proto::Payload payload;
		AddToPayload(payload, "program", program_name);
		SendMessage(sender, "I am here", payload);
		handled_message = true;
	}
	//===========================================================
	if(cmd=="I am here"){
		// We don't really need to do anything here since any message
		// from the server automatically updates the list and lastHeardForm
		// time above.
		handled_message = true;
	}
	//===========================================================
	if(cmd=="hists list"){
		RegisterHistList(sender, payload_items);
		handled_message = true;
	}
	//===========================================================
	if(cmd=="histogram"){
		// add to global variable so main ROOT thread can actually do registration
		RegisterHistogram(sender, payload_items);
		handled_message = true;
	}
	//===========================================================
	if(cmd=="histograms"){	
		RegisterHistograms(sender, payload_items);
		handled_message = true;
	}
#if 0
	//===========================================================
	if(cmd == "tree info") {
		RegisterTreeInfo(sender, msg);
		handled_message = true;
	}
	//===========================================================
	if(cmd == "tree") {
		RegisterTree(sender, msg);
		handled_message = true;
	}
#endif
	//===========================================================
	if(cmd == "macros list") {
		RegisterMacroList(sender, payload_items);
		handled_message = true;
	}
	//===========================================================
	if(cmd == "macro") {
		// add to global variable so main ROOT thread can actually do registration
		RegisterMacro(sender, payload_items);
		handled_message = true;
	}
	//===========================================================
	if(cmd=="final hists"){  // save histograms
	    _DBG_<<"received final histograms..."<<endl;
	    RegisterFinalHistogram(sender, payload_items);
	    handled_message = true;
	}
	//===========================================================
	// ignore messages sent by other RootSpy gui programs
	if(cmd=="who's there?" ) handled_message = true;
	if(cmd=="list hists"   ) handled_message = true;
	if(cmd=="list macros"  ) handled_message = true;
	if(cmd=="get tree info") handled_message = true;
	if(cmd=="get macro"    ) handled_message = true;
	if(cmd=="get hists"    ) handled_message = true;
	if(cmd=="get hist"     ) handled_message = true;
	//===========================================================
	if(!handled_message){
		_DBG_<<"Received unknown message --  cmd: " << cmd <<endl;
	}
}

//---------------------------------
// RegisterHistList
//---------------------------------
//TODO: documentation comment.
void rs_xmsg::RegisterHistList(string server, RSPayloadMap &payload_map)
{
	/// Copy list of histograms from xmsg into RS_INFO structures.

	// Confirm all items exist in payload
	int M = payload_map.count("hist_names")
		   + payload_map.count("hist_types")
		   + payload_map.count("hist_paths")
		   + payload_map.count("hist_titles");
	if( M != 4 ){
		if(verbose>0) _DBG_<<"Poorly formed response for \"hists list\". Ignoring."<<endl;
		return;
	}

	// Get pointers to Data objects for each item and
	// verify that that all have the same number of entries
	auto dhist_names  = payload_map["hist_names"];
	auto dhist_types  = payload_map["hist_types"];
	auto dhist_paths  = payload_map["hist_paths"];
	auto dhist_titles = payload_map["hist_titles"];
	auto N = dhist_names->stringa_size();
	if( ( dhist_types->stringa_size()  != N )
	 || ( dhist_paths->stringa_size()  != N )
	 || ( dhist_titles->stringa_size() != N )){
		if(verbose>0) _DBG_<<"Poorly formed response for \"hists list\" (sizes don't match!). Ignoring."<<endl;
		return;
	}

	if(verbose>2) _DBG_ << "got histogram list from " << server << endl;

	// Looks like we got a good response. Loop over histograms and add them to
	// list of hdef_t objects kept in RS_INFO. If there is already an entry
	// for a histogram, verify that the definition matches this new one.
	for(int i=0; i<N; i++){
	
		// Get name
		string name = dhist_names->stringa(i);

		// Get path without the preceeding root:
		string path = dhist_paths->stringa(i);

		size_t pos = path.find_first_of("/");
		if(pos!=string::npos)path = path.substr(pos);
		
		// Create temporary hdef_t object
		hdef_t hdef(name, path);
		const string &htype = dhist_types->stringa(i);
		if( htype.find("TH1")!=string::npos) hdef.type = hdef_t::oneD;
		else if( htype.find("TH2")!=string::npos) hdef.type = hdef_t::twoD;
		else if( htype.find("TH3")!=string::npos) hdef.type = hdef_t::threeD;
		else if( htype.find("TProfile")!=string::npos) hdef.type = hdef_t::profile;
		else hdef.type = hdef_t::noneD;

		hdef.title = dhist_titles->stringa(i);
		if(hist_default_active)
			hdef.active = true;
		else
			hdef.active = false;
		
		// Look for entry in RS_INFO
		RS_INFO -> Lock();
		if(RS_INFO->histdefs.find(hdef.hnamepath)==RS_INFO->histdefs.end()){
			if(verbose>3) _DBG_ << "Adding hdef with hnamepath=" << hdef.hnamepath << endl;
			RS_INFO->histdefs[hdef.hnamepath]=hdef;
		}else{
			if(verbose>6) _DBG_ << "Skipping adding of hdef with hnamepath=" << hdef.hnamepath << " since it already exists" << endl;
			// Need code to verify hdefs are same!!
		}
		
		// Make sure this server is in list of this hdef's servers
		map<string, bool> &servers = RS_INFO->histdefs[hdef.hnamepath].servers;
		if(servers.find(server)==servers.end())servers[server] = true;

		// Make sure this hdef is in list of this servers hdef's
		vector<string> &hnamepaths = RS_INFO->servers[server].hnamepaths;
		if(find(hnamepaths.begin(), hnamepaths.end(), hdef.hnamepath)==hnamepaths.end())hnamepaths.push_back(hdef.hnamepath);

		RS_INFO -> Unlock();
	}
}

#if 0
//---------------------------------
// RegisterTreeInfo
//---------------------------------
//TODO: documentation comment.
void rs_xmsg::RegisterTreeInfo(string server, RSPayloadMap &payload_map) {

	//TODO: (maybe) test that the msg is valid.
	RS_INFO->Lock();
	try{
	vector<tree_info_t> &rs_trees = RS_INFO->servers[server].trees;
		string name = msg->getString("tree_name");
		string path = msg->getString("tree_path");
		vector<string> branch_info = *(msg->getStringVector("branch_info"));
		vector<tree_info_t>::iterator veciter = rs_trees.begin();
		bool duplicate = false;
		for(; veciter != rs_trees.end(); veciter ++) {
			if(veciter->name.compare(name) == 0) duplicate = true;
		}
		if(!duplicate) {
	   	 // assume that branches are defined at initialization
	   	 // and don't change during running
	   	 rs_trees.push_back(tree_info_t(server, name, path, branch_info));

			 tree_id_t tid(server, name, path);
			 RS_INFO->treedefs[tid.tnamepath] = tid;
		} 
	}catch(xmsgException &e){
		_DBG_ << "Bad xmsg in RegisterTreeInfo from: " << server << endl;
		_DBG_ << e.what() << endl;
	}
	RS_INFO->Unlock();

//Test: check RS_INFO for trees
//	RS_INFO->Lock();
//	map<string, server_info_t>::iterator server_iter = RS_INFO->servers.begin();
//	for(; server_iter != RS_INFO->servers.end(); server_iter++) {
//		for(unsigned int i = 0; i< server_iter->second.trees.size(); i++) {
//			cerr<< "path: "<<server_iter->second.trees[i].path << endl;
//			cerr<< "name: "<<server_iter->second.trees[i].name << endl;
//			for(unsigned int j = 0;
//					j < server_iter->second.trees[i].branch_info.size(); j++) {
//				cout<<server_iter->second.trees[i].branch_info[j]<<endl;
//			}
//		}
//	}
//	RS_INFO->Unlock();
}
#endif

//---------------------------------
// RegisterMacroList
//---------------------------------
//TODO: documentation comment.
void rs_xmsg::RegisterMacroList(string server, RSPayloadMap &payload_map)
{
	/// Copy list of histograms from xmsg into RS_INFO structures.

	// Confirm all items exist in payload
	int M = payload_map.count("macro_names")
		   + payload_map.count("macro_paths");
	if( M != 2 ){
		if(verbose>0) _DBG_<<"Poorly formed response for \"macros list\". Ignoring."<<endl;
		return;
	}

	// Get pointers to Data objects for each item and
	// verify that that all have the same number of entries
	auto dmacro_names  = payload_map["macro_names"];
	auto dmacro_paths  = payload_map["macro_paths"];
	auto N = dmacro_names->stringa_size();
	if( dmacro_paths->stringa_size() != N ){
		if(verbose>0) _DBG_<<"Poorly formed response for \"macros list\" (sizes don't match!). Ignoring."<<endl;
		return;
	}

	if(verbose>=2)_DBG_ << "got macro list from " << server << endl;

	// Looks like we got a good response. Loop over histograms and add them to
	// list of hdef_t objects kept in RS_INFO. 
	// store similarly to histograms
	for(int i=0; i<N; i++){
	
		// Get name
		string name = dmacro_names->stringa(i);

		// Get path without the preceeding root:
		string path = dmacro_paths->stringa(i);

		size_t pos = path.find_first_of("/");
		if(pos!=string::npos) path = path.substr(pos);
		
		// Create temporary hdef_t object
		hdef_t macrodef(name, path);
		macrodef.type = hdef_t::macro;

		//hdef.title = (*hist_titles)[i];
		macrodef.title = "";
		if(hist_default_active)
			macrodef.active = true;
		else
			macrodef.active = false;
		
		// Look for entry in RS_INFO
		RS_INFO -> Lock();
		if(RS_INFO->histdefs.find(macrodef.hnamepath)==RS_INFO->histdefs.end()){
			RS_INFO->histdefs[macrodef.hnamepath]=macrodef;
			if(verbose>1)_DBG_ << "Added macro with hnamepath = " << macrodef.hnamepath << endl;
			RequestMacro("rootspy", macrodef.hnamepath);
		}else{
			// Need code to verify hdefs ae same!!
			// for now do some sanity check to at least make sure it is a macro
			if(macrodef.type != RS_INFO->histdefs[macrodef.hnamepath].type) {
				// give up!
				_DBG_ << "Got a macro with hnampath " << macrodef.hnamepath << " that was already defined as a histogram!" << endl
				      << "Exited..." << endl;
				return;
			}
		}
		
		// Make sure this server is in list of this hdef's servers
		map<string, bool> &servers = RS_INFO->histdefs[macrodef.hnamepath].servers;
		if(servers.find(server)==servers.end())
			servers[server] = true;

		// Make sure this hdef is in list of this servers hdef's
		vector<string> &hnamepaths = RS_INFO->servers[server].hnamepaths;
		if(find(hnamepaths.begin(), hnamepaths.end(), macrodef.hnamepath)==hnamepaths.end())
			hnamepaths.push_back(macrodef.hnamepath);

		RS_INFO -> Unlock();
	}
}



#if 0
//---------------------------------
// RegisterTreeInfo
//---------------------------------
//TODO: documentation comment.
void rs_xmsg::RegisterTreeInfoSync(string server, RSPayloadMap &payload_map) {

	//TODO: (maybe) test that the msg is valid.
	RS_INFO->Lock();
	vector<tree_info_t> &rs_trees = RS_INFO->servers[server].trees;

	vector<string> names;
	vector<string> paths;

	try{
		names = *(msg->getStringVector("tree_names"));
		paths = *(msg->getStringVector("tree_paths"));
	}catch(xmsgException e){
		// get here if tree_names or tree_paths doesn't exist
		_DBG_ << "Remote process reports no trees." << endl;
	}

	for( size_t numtree = 0; numtree < names.size(); numtree++) {
	    vector<tree_info_t>::iterator veciter = rs_trees.begin();
	    bool duplicate = false;
	    for(; veciter != rs_trees.end(); veciter ++) {
		if(veciter->name.compare(names[numtree]) == 0) duplicate = true;
	    }
	    if(!duplicate) {
		// assume that branches are defined at initialization
		// and don't change during running
		//_DBG_ << "tree info from " << server << " Tree " << names[numtree] << " in " << paths[numtree] << endl;
		
		// use blank branch info
		vector<string> branch_info;
		rs_trees.push_back(tree_info_t(server, names[numtree], paths[numtree], branch_info));
	    } 
	}

	RS_INFO->Unlock();
}

//---------------------------------
// RegisterTree
//---------------------------------
//TODO: documentation comment.
// Note that we only store tree info on a server-by-server basis,
// so this simplifies the code
void rs_xmsg::RegisterTree(string server, xmsg::Message &msg) 
{

    RS_INFO->Lock();

    string name = msg->getString("tree_name");
    string path = msg->getString("tree_path");

    if(verbose>2) _DBG_ << "got the following tree from " << server << ": " << name << " from " << path << endl;

    // Get pointer to server_info_t
    map<string,server_info_t>::iterator server_info_iter = RS_INFO->servers.find(server);
    if(server_info_iter==RS_INFO->servers.end()){
	_DBG_<<"No server_info_t object for server=\""<<server<<"\"!"<<endl;
	_DBG_<<"Throwing away tree."<<endl;
	RS_INFO->Unlock();
	return;
    }
    server_info_t *server_info = &(server_info_iter->second);

    
    // Get pointer to tree_info_t
    tree_info_t *tree_info = NULL;

    vector<tree_info_t> &rs_trees = RS_INFO->servers[server].trees;
    vector<tree_info_t>::iterator treeinfo_iter = rs_trees.begin();
    for(; treeinfo_iter != rs_trees.end(); treeinfo_iter ++) {
	if(treeinfo_iter->name.compare(name) == 0) 
	    break;
    }
    if(treeinfo_iter==rs_trees.end()){
	// tree_info_t object doesn't exist - add it
	vector<string> branch_info = *(msg->getStringVector("branch_info"));
	rs_trees.push_back(tree_info_t(server, name, path, branch_info));
	if(verbose>3){
		cout << "branch_info:" << endl;
		for(uint32_t i=0; i<branch_info.size(); i++){
			cout << "  " << branch_info[i] << endl;
		}
	}

	// get the pointer
	tree_info = &(rs_trees.back());
    } else {
	tree_info = &(*treeinfo_iter);
    }


    /**    
    tree_id_t tree_id(server, name, path);
    vector<tree_info_t>::iterator treeinfo_iter = RS_INFO->trees.find(tree_id_t);
    if(treeinfo_iter==RS_INFO->trees.end()){
	// tree_info_t object doesn't exist. Add one to RS_INFO
	RS_INFO->hinfos[hid] = hinfo_t(server, hnamepath); //???
	hinfo_iter = RS_INFO->hinfos.find(hid); //???
    }
    tree_info = &(*treeinfo_iter);
    **/


    // Get ROOT object from message and cast it as a TNamed*
    pthread_rwlock_wrlock(ROOT_MUTEX);

    if(verbose>1) _DBG_ << "unpacking tree..." << endl;

    MyTMessageXMSG *myTM = new MyTMessageXMSG(msg->getByteArray(),msg->getByteArrayLength());
    Long64_t length;
    TString filename;
    myTM->ReadTString(filename);
    myTM->ReadLong64(length);
    TDirectory *savedir = gDirectory;
    TMemFile *f = new TMemFile(filename, myTM->Buffer() + myTM->Length(), length);
    savedir->cd();
    
    TNamed *namedObj = NULL;
    
    TIter iter(f->GetListOfKeys());
    TKey *key;
    while( (key = (TKey*)iter()) ) {
	    string objname(key->GetName());
	    
	    cout << "TMemFile object: " << objname << endl;
	    if(objname == name) {
		    TObject *obj = key->ReadObj();
		    TNamed *nobj = dynamic_cast<TNamed*>(obj);
		    if(nobj != NULL) namedObj = nobj;
	    }
    }
    
    if(!namedObj){
	_DBG_<<"No valid object returned in tree message."<<endl;
	pthread_rwlock_unlock(ROOT_MUTEX);
	RS_INFO->Unlock();
	return;
    }
    
    // Cast this as a tree pointer
    TTree *T = dynamic_cast<TTree*>(namedObj);
    if(!T){
	_DBG_<<"Object received of type \""<<namedObj->ClassName()<<"\" is not a TTree type"<<endl;
	pthread_rwlock_unlock(ROOT_MUTEX);
	RS_INFO->Unlock();
	return;
    }

    if(verbose>2)_DBG_ << "unpacked tree!" << endl;
    T->Print();

    // Update tree_info
	double last_received = tree_info->received;
    tree_info->received = GetTime();
	tree_info->rate = 1.0/(tree_info->received - last_received);
    if(tree_info->tree){
	// Delete old histo
	delete tree_info->tree;
	tree_info->tree = NULL;
    }
    // update branch info?

    // Set pointer to hist in hinfo to new histogram
    tree_info->tree = T->CloneTree();
    
    // Change ROOT TDirectory of new histogram to server's
    tree_info->tree->SetDirectory(server_info->dir);

	if(f){
		f->Close();
		delete f;
	}

    // Unlock mutexes
    pthread_rwlock_unlock(ROOT_MUTEX);
    RS_INFO->Unlock();
}
#endif

//---------------------------------
// RegisterHistogram
//
// This is called when a message containing a histogram
// is received. It copies the information to the 
// HISTOS_TO_REGISTER_XMSG to be unpacked later.
//---------------------------------
void rs_xmsg::RegisterHistogram(string server, RSPayloadMap &payload_map)
{
	// Confirm all items exist in payload
	int M = payload_map.count("hnamepath") + payload_map.count("TMessage");
	if( M != 2 ){
		if(verbose>0) _DBG_<<"Poorly formed response for \"histogram\". Ignoring."<<endl;
		return;
	}

	// Get pointers to Data objects for each item and
	// verify that that all have the same number of entries
	auto dhnamepath  = payload_map["hnamepath"];
	auto dTMessage  = payload_map["TMessage"];
	
	if(verbose>3) _DBG_<<"Received histogram with TMessage payload of " << dTMessage->string().size() << " bytes for hnamepath " << dhnamepath->string() << endl;

	auto serialized = new rs_serialized;
	serialized->sender = server;
	serialized->hnamepath = dhnamepath->string();
	serialized->data.resize( dTMessage->string().size() );
	std::copy( dTMessage->string().begin(), dTMessage->string().end(), serialized->data.begin() );
	REGISTRATION_MUTEX_XMSG.lock();
	HISTOS_TO_REGISTER_XMSG.insert(serialized);
	REGISTRATION_MUTEX_XMSG.unlock();
}

//---------------------------------
// RegisterHistograms
//
// This is called when a message containing multiple histograms
// is received. It copies the information to the 
// HISTOS_TO_REGISTER_XMSG to be unpacked later.
//---------------------------------
void rs_xmsg::RegisterHistograms(string server, RSPayloadMap &payload_map)
{
	// The convention here is to have payloads with names like
	// "hnamepath1", "hnamepath2", ... and corresponding ones 
	// with names like "TMessage1", "TMessage2", ... This will
	// look for payloads with these names and add each that it
	// finds to HISTOS_TO_REGISTER_XMSG.
	
	REGISTRATION_MUTEX_XMSG.lock();
	auto Nstart = HISTOS_TO_REGISTER_XMSG.size();
	for(int i=1; i<1000; i++){ // loop should never get to 1000

		char pname_hnamepath[256];
		char pname_TMessage[256];
		snprintf(pname_hnamepath, 256, "hnamepath%d", i);
		snprintf(pname_TMessage, 256, "TMessage%d", i);

		// Confirm all items exist in payload
		int M = payload_map.count(pname_hnamepath) + payload_map.count(pname_TMessage);
		if( M != 2 ) break;

		// Get pointers to Data objects for each item and
		// verify that that all have the same number of entries
		auto dhnamepath  = payload_map[pname_hnamepath];
		auto dTMessage  = payload_map[pname_TMessage];

		auto serialized = new rs_serialized;
		serialized->sender = server;
		serialized->hnamepath = dhnamepath->string();
		serialized->data.resize( dTMessage->string().size() );
		std::copy( dTMessage->string().begin(), dTMessage->string().end(), serialized->data.begin() );
		HISTOS_TO_REGISTER_XMSG.insert(serialized);
	}
	if(HISTOS_TO_REGISTER_XMSG.size() == Nstart){
		if(verbose>0) _DBG_<<"Poorly formed response for \"histograms\". None found!"<<endl;
	}
	REGISTRATION_MUTEX_XMSG.unlock();                                                                                                                                
}

//---------------------------------
// MyCheckBinLabels
//---------------------------------
bool MyCheckBinLabels(TAxis *a1, TAxis *a2)
{
	THashList *l1 = a1->GetLabels();
	THashList *l2 = a2->GetLabels();

	if (!l1 && !l2 )
		return true;
	if (!l1 ||  !l2 ) {
		_DBG_<<" XXXXXXXXXXX - A " << endl;
		auto asrc  = l1 ? a1:a2;
		auto adest = l1 ? a2:a1;
		for (int i = 1; i <= asrc->GetNbins(); ++i) {
			adest->SetBinLabel( i, asrc->GetBinLabel(i) );
		}
		return false;
	}
	// check now labels sizes  are the same
	if (l1->GetSize() != l2->GetSize() ) {
		_DBG_<<" XXXXXXXXXXX - B " << endl;
		return false;
	}
	for (int i = 1; i <= a1->GetNbins(); ++i) {
		TString label1 = a1->GetBinLabel(i);
		TString label2 = a2->GetBinLabel(i);
		if (label1 != label2) {
			_DBG_<<" XXXXXXXXXXX - C " << endl;
			return false;
		}
	}

	return true;
}

//---------------------------------
// MyCheckAllBinLabels
//---------------------------------
bool MyCheckAllBinLabels(TH1 *h1, TH1 *h2)
{
	if (h1->GetDimension() != h2->GetDimension() ) return false;
	bool ret = true;
	switch( h1->GetDimension() ){
		case 3: ret &= MyCheckBinLabels( h1->GetZaxis(), h2->GetZaxis() );
		case 2: ret &= MyCheckBinLabels( h1->GetYaxis(), h2->GetYaxis() );
		case 1: ret &= MyCheckBinLabels( h1->GetXaxis(), h2->GetXaxis() );
	}

	if( !ret ) _DBG_ << "XXXXXXXXXXXXXXXX Mismatched labels for " << h1->GetName() << endl;

	return ret;
}

//---------------------------------
// RegisterHistogram
//
// This unpacks a histogram from the rs_serialized object
// and registers it with the system. This is called from
// the main ROOT thread while processing items from the
// HISTOS_TO_REGISTER_XMSG global.
//---------------------------------
void rs_xmsg::RegisterHistogram(rs_serialized *serialized)
{
	if(verbose>3)_DBG_ << "In rs_xmsg::RegisterHistogram()..." << endl;

	// Get hnamepath from message
	string &hnamepath = serialized->hnamepath;
	string &server = serialized->sender;

	if(hnamepath.find(": ") == 0 ) hnamepath.erase(0,2);

	if(verbose>=3) _DBG_ << "Registering histogram " << hnamepath << endl;

	// Lock RS_INFO mutex while working with RS_INFO
	RS_INFO->Lock();

	received_histograms[hnamepath]++;

	// Get pointer to hdef_t
	map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(hnamepath);
	if(hdef_iter==RS_INFO->histdefs.end()){
		_DBG_<<"No hdef_t object for hnamepath=\""<<hnamepath<<"\"!"<<endl;
		_DBG_<<"Throwing away histogram."<<endl;
		RS_INFO->Unlock();
		return;
	}
	hdef_t *hdef = &(hdef_iter->second);
    
	// Get pointer to server_info_t
	map<string,server_info_t>::iterator server_info_iter = RS_INFO->servers.find(server);
	if(server_info_iter==RS_INFO->servers.end()){
		_DBG_<<"No server_info_t object for server=\""<<server<<"\"!"<<endl;
		_DBG_<<"Throwing away histogram."<<endl;
		RS_INFO->Unlock();
		return;
	}
	server_info_t *server_info = &(server_info_iter->second);
    
	// Get pointer to hinfo_t
	hid_t hid(server, hnamepath);
	map<hid_t,hinfo_t>::iterator hinfo_iter = RS_INFO->hinfos.find(hid);
	if(hinfo_iter==RS_INFO->hinfos.end()){
		// hinfo_t object doesn't exist. Add one to RS_INFO
		RS_INFO->hinfos[hid] = hinfo_t(server, hnamepath);
		hinfo_iter = RS_INFO->hinfos.find(hid);
	}
	hinfo_t *hinfo = &(hinfo_iter->second);
    
	// Lock ROOT mutex while working with ROOT objects
	pthread_rwlock_wrlock(ROOT_MUTEX);

	// Get ROOT object from message and cast it as a TNamed*
	TNamed *namedObj = NULL;
	if(!serialized->data.empty()){
		// The TMessage will take ownership of the buffer and delete
		// it when it itself is deleted. Thus, we need to hand it
		// a buffer it can delete.
		int len = serialized->data.size();
		char *buff = new char[ len ];
		memcpy( buff, serialized->data.data(), len );
		if(verbose>3) _DBG_ << "Upacking TMessage of length " << len << " bytes" << endl;
		MyTMessageXMSG myTM( buff, len);
		namedObj = (TNamed*)myTM.ReadObject(myTM.GetClass());
		if(!namedObj){
			pthread_rwlock_unlock(ROOT_MUTEX);
			RS_INFO->Unlock();
			return;
		}
	}
    
	// Cast this as a histogram pointer
	TH1 *h = dynamic_cast<TH1*>(namedObj);
	if(!h){
		if(namedObj){
			_DBG_<<"Object received of type \""<<namedObj->ClassName()<<"\" is not a TH1 type"<<endl;
		}
		pthread_rwlock_unlock(ROOT_MUTEX);
		RS_INFO->Unlock();
		return;
	}
    
	// Update hinfo
	double last_received = hinfo->received;
	hinfo->received = GetTime();
	hinfo->rate = 1.0/(hinfo->received - last_received);
	if(hinfo->hist){
		// Subtract old histo from sum
		if( (hdef->sum_hist) && (hdef->type!=hdef_t::profile) ){
			if( ! MyCheckAllBinLabels( hdef->sum_hist,  hinfo->hist) ) MyCheckAllBinLabels( hdef->sum_hist,  hinfo->hist);
			hdef->sum_hist->Add(hinfo->hist, -1.0);
		}

		// Delete old histo
		delete hinfo->hist;
		hinfo->hist = NULL;
	}

	// Set pointer to hist in hinfo to new histogram
	hinfo->hist = h;

	// Change ROOT TDirectory of new histogram to server's
	hinfo->hist->SetDirectory(server_info->dir);

	// Adds the new histogram to the hists map in hdef_t
	map<string, hinfo_t>::iterator hinfo_it = hdef->hists.find(server);

	// first we have to check for and delete any older versions
	// of the hist.
	if(hinfo_it != hdef->hists.end()) {
		hdef->hists.erase(hinfo_it);
	}

	// Now we add the newer version to the map
	hdef->hists.insert(pair<string, hinfo_t>(server, (hinfo_iter->second)));
    
	// Add new histogram to sum and flag it as modified
	if(verbose>2) _DBG_<<"Adding "<<h->GetEntries()<<" from "<<server<<" to hist "<<hnamepath<<endl;
	if(hdef->sum_hist){
		// Reset sum histo first if showing only one histo at a time
		if(RS_INFO->viewStyle==rs_info::kViewByServer)hdef->sum_hist->Reset();
		
		// Need to check if hist is type profile and if so, reset it and sum all hists
		// together again. This is because we can't subtract the old histo from the
		// sum above like for non-profile hists without it screwing up the histogram.
		
		hdef->sum_hist->Add(h);
	}else{
		// get the directory in which to save the summed histogram

		// Make sure the full directory path exists
		string sum_path = "";
		TDirectory *hist_sum_dir = RS_INFO->sum_dir;
		for(uint32_t i=0; i<hdef->dirs.size(); i++){
			sum_path += "/" + hdef->dirs[i];
			TDirectory *dir = (TDirectory*)(hist_sum_dir->Get(hdef->dirs[i].c_str()));
			if(!dir) dir = hist_sum_dir->mkdir(hdef->dirs[i].c_str());
			hist_sum_dir = dir;
		}

		if(verbose>2) cout << "saving in directory " << sum_path << endl;

		string sum_hist_name = string(h->GetName())+"__sum";
		hdef->sum_hist = (TH1*)h->Clone(sum_hist_name.c_str());
		//hdef->sum_hist->SetDirectory(RS_INFO->sum_dir);
		hdef->sum_hist->SetDirectory(hist_sum_dir);
		hdef->sum_hist->SetName(h->GetName()); // set name back to original single hist name so macros work
	}

	// Record time we last modified the sum histo
	hdef->sum_hist_modified = GetTime();

	//Justin B
	hdef->sum_hist_present = true;

	// Unlock mutexes
	pthread_rwlock_unlock(ROOT_MUTEX);
	RS_INFO->Unlock();
}

//---------------------------------
// RegisterMacro
//---------------------------------
void rs_xmsg::RegisterMacro(string server, RSPayloadMap &payload_map) 
{
	// Confirm all items exist in payload
	int M = payload_map.count("macro_name")
		   + payload_map.count("macro_path")
		   + payload_map.count("TMessage");
	if( M != 3 ){
		if(verbose>0) _DBG_<<"Poorly formed response for \"macro\". Ignoring."<<endl;
		return;
	}

	// Get hnamepath from message
	string name = payload_map["macro_name"]->string();
	string path = payload_map["macro_path"]->string();
	string hnamepath = path + "/" + name;

	// Package up message into an rs_serialized object
	auto serialized = new rs_serialized;
	serialized->sender = server;
	serialized->hnamepath = hnamepath;
	auto dTMessage  = payload_map["TMessage"];
	serialized->data.resize( dTMessage->string().size() );
	std::copy( dTMessage->string().begin(), dTMessage->string().end(), serialized->data.begin() );
	REGISTRATION_MUTEX_XMSG.lock();
	MACROS_TO_REGISTER_XMSG.insert(serialized);
	REGISTRATION_MUTEX_XMSG.unlock();
}

//---------------------------------
// RegisterMacro
//---------------------------------
void rs_xmsg::RegisterMacro(rs_serialized *serialized) 
{
	/// This will unpack a xmsg containing a macro and 
	/// register it in the system. The macro will need
	/// have been declared previously via a RegisterMacroList
	/// call so the corresponding hdef_t can be accessed.
	///
	/// Note that this is not called by the xmsg thread itself
	/// but rather from the rs_mainframe::DoTimer() method.
	/// This is to prevent crashes due to the main ROOT event
	/// loop clashing with this thread (the main ROOT event
	/// loop is not halted by the rw_lock so the only way
	/// to get exclusive access to the ROOT global memory is
	/// to have that thread do it). This makes for the more
	/// complex system of registering the message into a 
	/// queue for later processing.

	if(verbose>=4) _DBG_ << "In rs_xmsg::RegisterMacro()..." << endl;

	string server = serialized->sender;
	string hnamepath = serialized->hnamepath;
    
	// Lock RS_INFO mutex while working with RS_INFO
	RS_INFO->Lock();
    
	received_macros[hnamepath]++;

	// Get pointer to hdef_t
	map<string,hdef_t>::iterator hdef_iter = RS_INFO->histdefs.find(hnamepath);
	if(hdef_iter==RS_INFO->histdefs.end()){
		_DBG_<<"No hdef_t object for hnamepath=\""<<hnamepath<<"\"!"<<endl;
		_DBG_<<"Throwing away macro."<<endl;
		RS_INFO->Unlock();
		return;
	}
	hdef_t *hdef = &(hdef_iter->second);

	// sanity check that this is a macro
	if(hdef->type != hdef_t::macro) {
		_DBG_ << " Tried to get macro with hnamepath=\"" << hnamepath
			<< "\", but it is already defined as a histogram!" << endl;
		RS_INFO->Unlock();
		return;
	}
    
	// Get pointer to server_info_t
	map<string,server_info_t>::iterator server_info_iter = RS_INFO->servers.find(server);
	if(server_info_iter==RS_INFO->servers.end()){
		_DBG_<<"No server_info_t object for server=\""<<server<<"\"!"<<endl;
		_DBG_<<"Throwing away macro."<<endl;
		RS_INFO->Unlock();
		return;
	}
    
	// Get pointer to hinfo_t
	hid_t hid(server, hnamepath);
	hinfo_t *hinfo = NULL;
	map<hid_t,hinfo_t>::iterator hinfo_iter = RS_INFO->hinfos.find(hid);
	if(hinfo_iter==RS_INFO->hinfos.end()){
		// hinfo_t object doesn't exist. Add one to RS_INFO
		RS_INFO->hinfos[hid] = hinfo_t(server, hnamepath);
		hinfo_iter = RS_INFO->hinfos.find(hid);
	}
	hinfo = &(hinfo_iter->second);
    
	// if we already have data for this macro, then throw it away
	// we need to do this before unpacking the new data, since we are reusing the file name
	if(hinfo->macroData) {
		hinfo->macroData->Close();
		delete hinfo->macroData;
		hinfo->macroData = NULL;
	}

	// save version info
	hinfo->macroVersion = 0.0; // versions not supported currently with xMsg (they were never used)

	// Lock ROOT mutex while working with ROOT objects
	pthread_rwlock_wrlock(ROOT_MUTEX);
    
	// extract info from message
	if(verbose>=2) _DBG_ << "unpacking macro \""<<hnamepath<<"\" ..." << endl;

		// The TMessage will take ownership of the buffer and delete
		// it when it itself is deleted. Thus, we need to hand it
		// a buffer it can delete.
		int len = serialized->data.size();
		char *buff = new char[ len ];
		memcpy( buff, serialized->data.data(), len );
		MyTMessageXMSG myTM( buff, len);
		Long64_t length;
		myTM.ReadLong64(length);
		TDirectory *savedir = gDirectory;

		// save each macro in a different file per server
		// we'll concatenate these later
		stringstream tmpfile_stream;
		tmpfile_stream << "." << server << "." << hnamepath;
		string tmpfile_name (tmpfile_stream.str());
		for(string::iterator str_itr = tmpfile_name.begin(); str_itr != tmpfile_name.end(); str_itr++) {
			// clean up filename by making sure that we at least dont have any '/'s
			if(*str_itr == '/') *str_itr = '_';
		}
		if(verbose>=4) _DBG_ << " TMemFile name = " << tmpfile_name << endl;
		if(verbose>=4) _DBG_ << "     file size = " << length << endl;
		TString filename(tmpfile_name);

		TMemFile *f = new TMemFile(filename, myTM.Buffer() + myTM.Length(), length);
		if(verbose>=4) _DBG_ << "     num. keys = " << f->GetNkeys() << endl;
		if(verbose>4){
			_DBG_ << "TMemFile contents: " << endl;
			f->ls();
		}

		if(savedir){
			savedir->cd();
		}else{
			// This has actually happened a few times. Not sure how.
			_DBG_<<"savedir=" << savedir << " (this shouldn't happen!)" << endl;
		}
    
		//TObjString *macro_str = new TObjString(the_macro.macro.c_str());
		TObjString *macro_str = (TObjString *)f->Get("macro");
		if(macro_str) hinfo->macroString = macro_str->GetString().Data();
		hinfo->macroData = f;
		hinfo->Nkeys = f->GetNkeys();
	
		// Look for special comments declaring hnamepaths this
		// macro requires.
		istringstream ss(hinfo->macroString);
		string line;
		uint32_t Nprev_macro_hnamepaths = hdef->macro_hnamepaths.size();
		hdef->macro_hnamepaths.clear();
		hdef->macro_guidance = "";
		bool in_guidance_section = false;
		while(getline(ss, line)){
			if( line.find("// Guidance:")==0 )in_guidance_section = true;
			if( in_guidance_section && (line.find("//")==0) ){
				hdef->macro_guidance += line + "\n";
			if( line.find("// End Guidance:")==0 )in_guidance_section = false;
			continue; // don't consider other keywords if in guidance section
		}
		
		// guidance stops if we hit non-comment line
		in_guidance_section = false;

		if(line.find("// hnamepath:")==0){
			string myhnamepath = line.substr(13);
			myhnamepath.erase(myhnamepath.find_last_not_of(" \n\r\t")+1);
			if(myhnamepath.find(" ")==0) myhnamepath = myhnamepath.substr(1);
			hdef->macro_hnamepaths.insert(myhnamepath);
			if(verbose>1) _DBG_<<"Added \"" <<  myhnamepath << "\" to macro: " << hnamepath << endl;
		}
		if(line.find("// e-mail:")==0 || line.find("// email:")==0){
			string myemail = line.substr(10);
			myemail.erase(myemail.find_last_not_of(" \n\r\t")+1);
			hdef->macro_emails.insert(myemail);
			if(verbose>1) _DBG_<<"Added notification e-mail " <<  myemail << " to macro: " << hnamepath << endl;
		}
	}

	// If we have a different number of macro input histograms
	// than before then spin off a thread to start asking for
	// them. This should usually happen only at start up when
	// a macro is read in for the first time. This helps by 
	// pre-loading histograms that will be needed for display.
	if(hdef->macro_hnamepaths.size() != Nprev_macro_hnamepaths){
		if(verbose>1)_DBG_<<"seeding ... hdef->macro_hnamepaths.size()="<<hdef->macro_hnamepaths.size()<<" Nprev_macro_hnamepaths="<<Nprev_macro_hnamepaths<<endl;
		thread t(&rs_xmsg::SeedHnamepathsSet, this, (void*)&hdef->macro_hnamepaths, true, false);
		t.detach();
	}

	// Update hinfo
	double last_received = hinfo->received;
	hinfo->received = GetTime();
	hinfo->rate = 1.0/(hinfo->received - last_received);
   
	// Adds the new histogram to the hists map in hdef_t
	map<string, hinfo_t>::iterator hinfo_it = hdef->hists.find(server);
    
	// first we have to check for and delete any older versions
	// of the hist.
	if(hinfo_it != hdef->hists.end()) {
		hdef->hists.erase(hinfo_it);
	}
    
	// Now we add the newer version to the map
	hdef->hists.insert(pair<string, hinfo_t>(server, (hinfo_iter->second)));

	// Record time we last modified the sum histo
	hdef->sum_hist_modified = GetTime();
    
	// Unlock mutexes
	pthread_rwlock_unlock(ROOT_MUTEX);
	RS_INFO->Unlock();
}



//---------------------------------
// RegisterFinalHistogram
//---------------------------------
//TODO: documentation comment.
void rs_xmsg::RegisterFinalHistogram(string server, RSPayloadMap &payload_map)
{

	// first, save the histogram in memory
	RegisterHistogram(server, payload_map);

	// Confirm all items exist in payload
	int M = payload_map.count("hnamepath")
		   + payload_map.count("TMessage");
	if( M != 2 ){
		if(verbose>0) _DBG_<<"Poorly formed response for \"final histos\". Ignoring."<<endl;
		return;
	}

	// Now, store the histogram in a list for optional processing by the client program
	// Get hnamepath from message
	string hnamepath = payload_map["hnamepath"]->string();

	// Lock RS_INFO mutex while working with RS_INFO
	RS_INFO->Lock();
    
	// Lock ROOT mutex while working with ROOT objects
	pthread_rwlock_wrlock(ROOT_MUTEX);
    
	// Get ROOT object from message and cast it as a TNamed*
	auto dTMessage  = payload_map["TMessage"];


	// The TMessage will take ownership of the buffer and delete
	// it when it itself is deleted. Thus, we need to hand it
	// a buffer it can delete.
	int len = dTMessage->string().size();
	char *buff = new char[ len ];
	memcpy( buff, dTMessage->string().data(), len );
	MyTMessageXMSG myTM( buff, len);
	TNamed *namedObj = (TNamed*)myTM.ReadObject(myTM.GetClass());
	if(!namedObj){
		_DBG_<<"No valid object returned in histogram message."<<endl;
		pthread_rwlock_unlock(ROOT_MUTEX);
		RS_INFO->Unlock();
		return;
	}
    
	// Cast this as a histogram pointer
	TH1 *h = dynamic_cast<TH1*>(namedObj);
	if(!h){
		_DBG_<<"Object received of type \""<<namedObj->ClassName()<<"\" is not a TH1 type"<<endl;
		pthread_rwlock_unlock(ROOT_MUTEX);
		RS_INFO->Unlock();
		return;
	}
    
	// save histogram info for consumer thread
	RS_INFO->final_hists.push_back(final_hist_t( server, hnamepath, h ));

	// Unlock mutexes
	pthread_rwlock_unlock(ROOT_MUTEX);
	RS_INFO->Unlock();
}

//---------------------------------
// SeedHnamepathsSet
//---------------------------------
void rs_xmsg::SeedHnamepathsSet(void *vhnamepaths, bool request_histo, bool request_macro)
{
	set<string> &hnamepaths = *(set<string>*)vhnamepaths;

	/// This is just a wrapper for the "list" version below.
	list<string> lhnamepaths;
	set<string>::iterator iter = hnamepaths.begin();
	for(; iter!=hnamepaths.end(); iter++) lhnamepaths.push_back(*iter);
	
	SeedHnamepaths(lhnamepaths, request_histo, request_macro);
}

//---------------------------------
// SeedHnamepaths
//---------------------------------
void rs_xmsg::SeedHnamepaths(list<string> &hnamepaths, bool request_histo, bool request_macro)
{
	/// This is run in a separate, temporary thread
	/// to request the histograms and/or macros with
	/// the given hnamepaths. This is called either
	/// from ReadConfig as a new RSTab's configuration
	/// is read in, or from RegisterMacro when a macro
	/// is first read that specifies a set of hnamepaths
	/// it needs. The intent is for them to be read 
	/// in the background when the program first starts
	/// up so that by the time the user asks for them,
	/// they may already be here making the program
	/// display them faster.
	///
	/// This loops over the given hnamepaths requesting
	/// each from the generic "rootspy" since we don't
	/// yet know what producers are there. It may also
	/// request the hnamepath as both a histogram and
	/// a macro (depending on the supplied flags) since
	/// we may not actually know at this point which
	/// it is!

	// If requesting histos, a single request may be sent for
	// all of them.
	if(request_histo){

		// convert from STL list to STL vector container
		vector<string> shnamepaths;
		for(auto h : hnamepaths) shnamepaths.push_back( h );
		
		// Request histos
		if(verbose>1) _DBG_ << "SeedHnamepaths: requesting " << shnamepaths.size() << " histos" << endl;
		if(verbose>2) for(auto s : shnamepaths) _DBG_ << "   --  " << s << endl;
		RequestHistograms("rootspy", shnamepaths);
	}

	// Optionally request macro
	if(request_macro){
		for(auto hnamepath : hnamepaths){
		
			if(verbose>0) _DBG_ << "SeedHnamepaths: requesting macro " << hnamepath << endl;
			RequestMacro("rootspy", hnamepath);
		}
	}
}

//---------------------------------
// DirectUDPServerThread
//---------------------------------
void rs_xmsg::DirectUDPServerThread(void)
{
	/// This method is run in a thread to listen for
	/// UDP packets sent here directly from a remote
	/// histogram producer. This allows the bulky
	/// histogram messages to be sent this way instead
	/// of through the xmsg server.

	if(netdevices.empty()) return; // extra bomb-proofing

	rs_netdevice *dev = netdevices[0];
	
	// Create socket
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(fd<0){
		perror("unable to create socket for UDP direct!");
		return;
	}
	
	// Make socket non-blocking
	if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0 ){
		perror("can't set socket to non-blocking!");
		return;
	}
	
	// Bind to address and have system assign port number
	struct sockaddr_in myaddr;
	memset((char*)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY); // accept packets from any device
	myaddr.sin_port = 0; // let system assign a port
	if( ::bind(fd, (struct sockaddr*)&myaddr, sizeof(myaddr)) < 0){
		perror("bind failed");
		close(fd);
		return;
	}
	
	// Get port number
	socklen_t socklen = sizeof(myaddr);
	if(getsockname(fd, (struct sockaddr*)&myaddr, &socklen) < 0){
		perror("getsockname failed");
		close(fd);
		return;
	}
	udpport = myaddr.sin_port;

	cout << "Launched UDP server using " << dev->name << " - " << dev->ip_dotted << " : " << udpport << endl;
	
	// upddev is used as flag to indicate we're able to
	// receive histograms this way. If we don't get here,
	// it will be left as NULL and the direct UDP feature
	// will not be used.
	udpdev = dev;
	
	uint32_t buff_size = 100000;
	char *buff = new char[buff_size];
	
	while(!stop_udpthread){
	
		// Try and read some data
		socklen = sizeof(myaddr);
		ssize_t bytes_recvd = recvfrom(fd, buff, buff_size, 0, (struct sockaddr*)&myaddr, &socklen);
		if(bytes_recvd < 0){
			// Seems dangerous to check errno here due to
			// there being multiple threads ....
			if( (errno==EAGAIN) || (errno==EWOULDBLOCK) ){
				// no data. sleep for 10ms
				usleep(10000);
				continue;
			}else{
				perror("receiving on rootspy direct UDP");
				break;
			}
		}
		
		if(verbose>4) _DBG_ << "received UDP with " << bytes_recvd << " bytes!" << endl;
		
		rs_udpmessage *rsudp = (rs_udpmessage*)buff;
		string mtype = "unknown";
		switch(rsudp->mess_type){
			case kUDP_HISTOGRAM: mtype="histogram"; break;
			case kUDP_MACRO    : mtype="macro";     break;
		}
		string hnamepath = (const char*)rsudp->hnamepath;
		string sender = (const char*)rsudp->sender;
		char *buff8 = (char*)&rsudp->buff_start;
		if(verbose>2) _DBG_ << "UDP: hnamepath=" << rsudp->hnamepath << " type=" << mtype << " from " << sender << endl;

		// Write info into a rs_serialized object so we can
		// let the same RegisterHistogram method handle
		// it as handles histograms coming from xmsg itself.
		auto serialized = new rs_serialized;
		serialized->sender = sender;
		serialized->hnamepath = hnamepath;
		serialized->data.reserve(rsudp->buff_len);
		std::copy( buff8, buff8 + rsudp->buff_len, std::back_inserter(serialized->data) );

		REGISTRATION_MUTEX_XMSG.lock();
		HISTOS_TO_REGISTER_XMSG.insert(serialized);  // defer to main ROOT thread
		REGISTRATION_MUTEX_XMSG.unlock();		
	}
	
	close(fd);
	delete[] buff;
}


//---------------------------------
// DirectTCPServerThread
//---------------------------------
void rs_xmsg::DirectTCPServerThread(void)
{
	/// This method is run in a thread to listen for
	/// UDP packets sent here directly from a remote
	/// histogram producer. This allows the bulky
	/// histogram messages to be sent this way instead
	/// of through the xmsg server.

	if(netdevices.empty()) return; // extra bomb-proofing

	rs_netdevice *dev = netdevices[0];

	// Create socket
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(fd<0){
		perror("unable to create socket for TCP direct!");
		return;
	}
	
	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,  &opt, sizeof(opt)) ){
		perror("setsockopt failed");
		close(fd);
		return;
	}
	
	// Make socket non-blocking
	if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0 ){
		perror("can't set socket to non-blocking!");
		return;
	}
	
	// Bind to address and have system assign port number
	struct sockaddr_in myaddr;
	memset((char*)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY); // accept packets from any device
	myaddr.sin_port = 0; // let system assign a port
	if( ::bind(fd, (struct sockaddr*)&myaddr, sizeof(myaddr)) < 0){
		perror("bind failed");
		close(fd);
		return;
	}
	
	// Get port number
	socklen_t socklen = sizeof(myaddr);
	if(getsockname(fd, (struct sockaddr*)&myaddr, &socklen) < 0){
		perror("getsockname failed");
		close(fd);
		return;
	}
	tcpport = myaddr.sin_port;
	
	// Listen for connections
	if( ::listen(fd, 32) < 0 ){
		perror("listen failed");
		close(fd);
		return;
	}

	cout << "Launched TCP server using " << dev->name << " - " << dev->ip_dotted << " : " << tcpport << endl;
	
	// tcpdev is used as flag to indicate we're able to
	// receive histograms this way. If we don't get here,
	// it will be left as NULL and the direct TCP feature
	// will not be used.
	tcpdev = dev;
	
	uint32_t buff_size = 10000000;
	char *buff = new char[buff_size];
	
	while(!stop_tcpthread){
	
		// Try and read some data
		socklen_t myaddr_len = sizeof(myaddr);
		int fd_new = accept(fd, (struct sockaddr*)&myaddr, &myaddr_len);
		if(fd_new < 0){
			// Seems dangerous to check errno here due to
			// there being multiple threads ....
			if( (errno==EAGAIN) || (errno==EWOULDBLOCK) ){
				// no data. sleep for 10ms
				usleep(10000);
				continue;
			}else{
				perror("receiving on rootspy direct TCP");
				break;
			}
		}
		
		int bytes_recvd = read( fd_new, buff, buff_size);
		
		if(verbose>4) _DBG_ << "received TCP with " << bytes_recvd << " bytes" << endl;
		
		// Note: We reuse the rs_udpmessage data structure here since an rs_tcpmessage would be identical
		rs_udpmessage *rsudp = (rs_udpmessage*)buff;
		string mtype = "unknown";
		switch(rsudp->mess_type){
			case kUDP_HISTOGRAM: mtype="histogram"; break;
			case kUDP_MACRO    : mtype="macro";     break;
		}
		string hnamepath = (const char*)rsudp->hnamepath;
		string sender = (const char*)rsudp->sender;
		char *buff8 = (char*)&rsudp->buff_start;
		if(verbose>2) _DBG_ << "TCP: hnamepath=" << rsudp->hnamepath << " type=" << mtype << " from " << sender << endl;
		
		// It's possible large messages could be broken up by the network into
		// chunks arriving at different times. Verify that we have the whole 
		// buffer.
		int buff_len_transferred = bytes_recvd + sizeof(uint32_t*) - sizeof(rs_udpmessage);
		for(int i=0; i<100; i++){
			if( (uint32_t)buff_len_transferred >= rsudp->buff_len ) break;
			
			int my_bytes_recvd = read( fd_new, &buff[bytes_recvd], buff_size-bytes_recvd);
			if( my_bytes_recvd < 0) { usleep(1); continue; }
			bytes_recvd += my_bytes_recvd;
			buff_len_transferred = bytes_recvd + sizeof(uint32_t*) - sizeof(rs_udpmessage);
			if(verbose>4) _DBG_ << "added to TCP buffer " << my_bytes_recvd << " bytes (" << bytes_recvd << " total)" << endl;
		}

		if( (uint32_t)buff_len_transferred < rsudp->buff_len ){
			cerr << "ERROR: TCP buffer received smaller than expected (" << buff_len_transferred << " < " << rsudp->buff_len << ")" << endl;
			cerr << "       message with hnamepath: " << hnamepath << " discarded" << endl;
			close( fd_new );
			continue;
		}

		// Write info into a rs_serialized object so we can
		// let the same RegisterHistogram method handle
		// it as handles histograms coming from xmsg itself.
		auto serialized = new rs_serialized;
		serialized->sender = sender;
		serialized->hnamepath = hnamepath;
		serialized->data.reserve(rsudp->buff_len);
		std::copy( buff8, buff8 + rsudp->buff_len, std::back_inserter(serialized->data) );

		REGISTRATION_MUTEX_XMSG.lock();
		HISTOS_TO_REGISTER_XMSG.insert(serialized);  // defer to main ROOT thread
		REGISTRATION_MUTEX_XMSG.unlock();		
	}
	
	close(fd);
	delete[] buff;
}


