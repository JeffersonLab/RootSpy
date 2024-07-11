// $Id$
//
//    File: rs_influxdb.h
// Created: Tue Aug 29 09:36:37 EDT 2017
// Creator: davidl (on Linux gluon112.jlab.org 2.6.32-642.3.1.el6.x86_64 x86_64)
//
//
//  This class is used to make entries into the influxdb that stores
// time series data. We use it in GlueX to store values from the online
// monitoring system like the means and widths of fits to resonances.
// This allows drifts and trends to be seen during the run.
//
// Most of the heavy lifting is done by the libcurl library. To use this,
// instantiate an rs_influxdb object giving it the host, port, and database
// name. Then, just call the AddData method for each item you wish to
// write. AddData has teo forms. The first takes 3 or 4 arguments:
//
//  item name
//  map of tags (optional)
//  map of values
//  time (in secs, UTC. usally whats returned by time(NULL) )
//
// The map of tags can be empty (though a reference to an empty container
// must still be passed in). The map of values has a templated type for the
// actual values so anything that can be converted using stringstream is
// allowed. The one limitation is that since it is a map, all values must
// be the same type which is not something strictly required by influxdb. If
// you wish to store fields with different types, make the template parameter
// a string type and convert them yourself. Influxdb will automatically
// determine the types from the stringified query form anyway.
//
// The second AddData method takes a single string that has already been
// formatted to contain the data, tags, and time. This form is used when
// trying to write to the DB from a RootSpy macro. Testing with ROOT 6.10.00
// showed problems when trying to pass containers when calling a compiled
// routine from a macro, but passing a string works fine. An example of
// the macro code needed for this is:
//
//		// Add to time series
//		stringstream ss;
//		ss << "mass_fit,ptype=pi0 mass="<<pars[1]<<",width="<<pars[2]<<" "<<time(NULL);
//		InsertSeriesData(ss.str());
//
// where InsertSeries() has the following format, already defined in
// the interpreter for all macros:
//
//   void InsertSeriesData(string sdata);
//
//
// It is strongly recommended to create any databases on the influxdb server
// you will be writing to using the influx command line tool ahead of time.
// The constructor will attempt to this automatically, but it does so in
// a sketchy way that should probably be removed. 


#ifndef _rs_influxdb_
#define _rs_influxdb_

#include <map>
#include <mutex>
#include <string>
#include <cstdint>
using namespace std;

#include <curl/curl.h>

//=======================================================
// Global scope routines
void InsertSeriesData(string sdata);
void InsertSeriesData(string item, map<string, string> tags, map<string,double> vals, double unix_time=0.0);
void InsertSeriesMassFit(string ptype, double mass, double width, double mass_err, double width_err, double unix_time=0.0);
//=======================================================


class rs_influxdb{
	public:
		
		rs_influxdb(const string &host, const string &database, uint32_t port=0);
		virtual ~rs_influxdb();

		template<typename T>
		int AddData(const string &item, const map<string, string> &tags, const map<string,T> &vals, double unix_time=0.0);
		int AddData(const string &sdata);


		//................................................................................

		// member data
		string host;
		string database;
		uint32_t port;
		string url;
		CURL *curl;
		mutex mtx;
		
		bool require_unix_time;
};


#endif // _rs_influxdb_

