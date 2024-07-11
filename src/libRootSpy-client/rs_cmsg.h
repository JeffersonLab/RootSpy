// $Id$
//
//    File: rs_cmsg.h
// Created: Thu Aug 28 20:00 EDT 2009
// Creator: davidl (on Darwin harriet.jlab.org 9.8.0 i386)
//

#ifndef _rs_cmsg_
#define _rs_cmsg_

#include <sys/time.h>

#include "hinfo_t.h"
#include "rs_netdevice.h"
#include "rs_udpmessage.h"

#include <vector>
#include <set>
#include <string>
#include <thread>

#ifdef HAVE_CMSG
#include <cMsg.h>
#include <cMsg.hxx>
using namespace cmsg;
#endif // HAVE_CMSG


typedef struct timespec timespec_t;

#include <TMessage.h>

class rs_mainframe;

// NOTE: We want this to compile with or without cMsg 
// support. At this point in time xMsg is being added
// which requires -std=c++14 making it incompatible with
// the cMsg libraries built with -std=c++11. Hence the
// heavy use of preprocessor masks based on HAVE_CMSG


#ifdef HAVE_CMSG
class rs_cmsg:public cMsgCallback{
#else
class rs_cmsg{
#endif
	public:
		rs_cmsg(string &udl, string &name, bool connect_to_cmsg=true);
		virtual ~rs_cmsg();

		// normal requests (async)
		void PingServers(void);
		void RequestHists(string servername);
		void RequestHistogram(string servername, string hnamepath);
		void RequestHistograms(string servername, vector<string> &hnamepaths);
		void FinalHistogram(string servername, vector<string> hnamepath);
		void RequestTreeInfo(string servername);
		void RequestTree(string servername, string tree_name, string tree_path, int64_t num_entries);
		void RequestMacroList(string servername);
		void RequestMacro(string servername, string hnamepath);

		bool   IsOnline(void)   { return is_online; }
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
		uint16_t udpport;
		std::thread *udpthread;
		bool stop_udpthread;

		rs_netdevice *tcpdev;
		uint16_t tcpport;
		std::thread *tcpthread;
		bool stop_tcpthread;
		
	public:

#ifdef HAVE_CMSG
		cMsg* GetcMsgPtr(void) { return cMsgSys;   }
		void callback(cMsgMessage *msg, void *userObject);
		void RegisterHistList(string server, cMsgMessage *msg);
		void RegisterHistogram(string server, cMsgMessage *msg, bool delete_msg=false);
		void RegisterHistograms(string server, cMsgMessage *msg);
		void RegisterFinalHistogram(string server, cMsgMessage *msg);
		void RegisterTreeInfo(string server, cMsgMessage *msg);
		void RegisterTreeInfoSync(string server, cMsgMessage *msg);
		void RegisterTree(string server, cMsgMessage *msg);
		void RegisterMacroList(string server, cMsgMessage *msg);
		void RegisterMacro(string server, cMsgMessage *msg);

		void BuildRequestHists(cMsgMessage &msg, string servername);
		void BuildRequestHistogram(cMsgMessage &msg, string servername, string hnamepath);
		void BuildRequestHistograms(cMsgMessage &msg, string servername, vector<string> &hnamepaths);
		void BuildRequestTreeInfo(cMsgMessage &msg, string servername);
		void BuildRequestTree(cMsgMessage &msg, string servername, string tree_name, string tree_path, int64_t num_entries);
		void BuildRequestMacroList(cMsgMessage &msg, string servername);
		void BuildRequestMacro(cMsgMessage &msg, string servername, string hnamepath);

	private:
		cMsg *cMsgSys;
		cMsgSubscriptionConfig *cMsgSubConfig;
#endif // HAVE_CMSG

	public:
		void SeedHnamepathsSet(void *vhnamepaths, bool request_histo, bool request_macro);
		void SeedHnamepaths(list<string> &hnamepaths, bool request_histo, bool request_macro);

		void DirectUDPServerThread(void);
		void DirectTCPServerThread(void);
		
		bool is_online;
		string myname;
		
		std::vector<void*> subscription_handles;
};


class MyTMessage : public TMessage {
public:
   MyTMessage(void *buf, Int_t len) : TMessage(buf, len) { }
};


#endif // _rs_cmsg_

