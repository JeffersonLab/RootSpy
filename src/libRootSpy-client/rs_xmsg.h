// $Id$
//
//    File: rs_xmsg.h
// Created: Tue Mar 13
// Creator: davidl 
//

#ifndef _rs_xmsg_
#define _rs_xmsg_

#include <sys/time.h>

#include "hinfo_t.h"
#include "rs_netdevice.h"
#include "rs_udpmessage.h"
#include "rs_serialized.h"

#include <vector>
#include <set>
#include <string>
#include <thread>

#include <memory> 
#include <xmsg/xmsg.h>
#include <xmsg/util.h>
using namespace xmsg;

typedef struct timespec timespec_t;

#include <TMessage.h>

class rs_mainframe;

typedef map<string, const xmsg::proto::Data*> RSPayloadMap;

class rs_xmsg{
	public:
		rs_xmsg(string &udl, string &name, bool connect_to_xmsg=true);
		virtual ~rs_xmsg();
		
		rs_xmsg(const rs_xmsg &src_msg){
			cerr<< __FILE__ << ":" << __LINE__ <<" this=" << this << " src=" << &src_msg << endl;
		}
		
		static rs_xmsg* GetGlobalPtr(void){ return _rs_xmsg_global; }

		// normal requests (async)
		void PingServers(void);
		void RequestHists(string servername);
		void RequestHistogram(string servername, string hnamepath);
		void RequestHistograms(string servername, vector<string> &hnamepaths);
		void FinalHistogram(string servername, vector<string> hnamepath);
		void RequestTreeInfo(string servername);
		void RequestTree(string servername, string tree_name, string tree_path, int64_t num_entries);
		void RequestMacroList(string servername);
		void RequestMacro(string servername, string hnamepath, bool include_histos=true);

		bool   IsOnline(void)   { return is_online; }
		xMsg*  GetxmsgPtr(void) { return xmsgp;     }
		string GetMyName(void)  { return myname;    }

		// Static method to return time in seconds with microsecond accuracy
		static double GetTime(void){
			struct timeval tval;
			struct timezone tzone;
			gettimeofday(&tval, &tzone);
			double t = (double)tval.tv_sec+(double)tval.tv_usec/1.0E6;
			if(start_time==0.0) start_time = t;
			return t - start_time;
		}

		bool hist_default_active;
		int verbose;
		static double start_time;
		string program_name;
		map<string,uint32_t> requested_histograms;
		map<string,uint32_t> received_histograms;
		map<string,uint32_t> requested_macros;
		map<string,uint32_t> received_macros;

		vector<rs_netdevice*> netdevices;
		rs_netdevice *udpdev;
		::google::protobuf::int32 udpport;
		std::thread *udpthread;
		bool stop_udpthread;

		rs_netdevice *tcpdev;
		::google::protobuf::int32 tcpport;
		std::thread *tcpthread;
		bool stop_tcpthread;
		
	public:

		// The following defines the xMsg callback method
		void callback(xmsg::Message &msg);

		void RegisterHistList(string server, RSPayloadMap &payload_map);
		void RegisterHistogram(string server, RSPayloadMap &payload_map);
		void RegisterHistograms(string server, RSPayloadMap &payload_map);
		void RegisterHistogram(rs_serialized *serialized);
		void RegisterFinalHistogram(string server, RSPayloadMap &payload_map);
		void RegisterTreeInfo(string server, RSPayloadMap &payload_map);
		void RegisterTreeInfoSync(string server, RSPayloadMap &payload_map);
		void RegisterTree(string server, RSPayloadMap &payload_map);
		void RegisterMacroList(string server, RSPayloadMap &payload_map);
		void RegisterMacro(string server, RSPayloadMap &payload_map);
		void RegisterMacro(rs_serialized *serialized);

#if 0
		void BuildRequestHists(xmsg::Message &msg, string servername);
		void BuildRequestHistogram(xmsg::Message &msg, string servername, string hnamepath);
		void BuildRequestHistograms(xmsg::Message &msg, string servername, vector<string> &hnamepaths);
		void BuildRequestTreeInfo(xmsg::Message &msg, string servername);
		void BuildRequestTree(xmsg::Message &msg, string servername, string tree_name, string tree_path, int64_t num_entries);
		void BuildRequestMacroList(xmsg::Message &msg, string servername);
		void BuildRequestMacro(xmsg::Message &msg, string servername, string hnamepath);
#endif
		void SeedHnamepathsSet(void *vhnamepaths, bool request_histo, bool request_macro);
		void SeedHnamepaths(list<string> &hnamepaths, bool request_histo, bool request_macro);

		void DirectUDPServerThread(void);
		void DirectTCPServerThread(void);
		
		template<typename V> 
		void SendMessage(string servername, string command, V&& data, string dataType="mystery");
		void SendMessage(string servername, string commands);
		void SendMessage(string servername, string command, xmsg::proto::Payload &payload);

		template<typename V>
		void AddToPayload(xmsg::proto::Payload &payload, string name, V &data);
		
		

	protected:
		xmsg::xMsg *xmsgp;
		xmsg::ProxyConnection *pub_con;
		
		bool is_online;
		string myname;
		mutex pub_mutex;
		
		std::vector< xmsg::Subscription* > subscription_handles;
	
	private:
		static rs_xmsg *_rs_xmsg_global;
};


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
void rs_xmsg::SendMessage(string servername, string command, V&& data, string dataType)
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
	if(is_online){
		if(verbose>3) cerr << "Sending \"" << command << "\"" << endl;
		if(pub_con){
			lock_guard<mutex> lck(pub_mutex);
			xmsgp->publish(*pub_con, msg);
		}else{
			cerr << "connection for publishing not set. command \"" << command << "\" not sent." << endl;
		}
	}
}

//---------------------------------
// AddToPayload
//
// Add an item to the given Payload object
//---------------------------------
template<typename V>
void rs_xmsg::AddToPayload(xmsg::proto::Payload &payload, string name, V &data)
{
	auto item = payload.add_item();
	item->set_name(name);
	auto mydata = xmsg::proto::make_data(data);
	item->mutable_data()->CopyFrom( mydata );
}

class MyTMessageXMSG : public TMessage {
public:
   MyTMessageXMSG(void *buf, Int_t len) : TMessage(buf, len) { }
};


#endif // _rs_xmsg_

