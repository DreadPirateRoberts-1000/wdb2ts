/*
    wdb - weather and water data storage

    Copyright (C) 2007 met.no

    Contact information:
    Norwegian Meteorological Institute
    Box 43 Blindern
    0313 OSLO
    NORWAY
    E-mail: wdb@met.no
  
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, 
    MA  02110-1301, USA
*/
#include <sstream>
#include <msleep.h>
#include <DbManager.h>

using namespace std;
using namespace miutil::pgpool;

namespace {
class ConExtra : public miutil::pgpool::ConnectionExtra {
	std::string wciuser;
	
public:

	ConExtra(const std::string &wciuser_) 
		: wciuser( wciuser_ ) {}
	virtual ~ConExtra(){}
	
	///Called when a new connection is created on the server.
   virtual void onCreateConnection( pqxx::connection &con ) 
   {
   	try {
   	      pqxx::work x( con, "WciBeginEndHelper" );
   	      
   	      x.exec("SELECT wci.begin('" + wciuser + "');");
   	      x.commit();
   	      //cerr << " WciBeginEndHelper: Execute wci.begin('" << wciuser << "')." << endl;
   	   }
   	   catch ( exception & e ) {
   		   ostringstream errorMsg;
   		   errorMsg << "CTOR: Exception (WciBeginEndHelper): ";
   		   errorMsg << "Cant execute wci.begin('" << wciuser << "'). ";
   		   errorMsg << "Reason: " << e.what();
   		   throw logic_error( errorMsg.str() );
   	   }
   	   catch( ... ) {
   	      throw logic_error("CTOR: Exception (WciBeginEndHelper): Cant execute wci.begin('" + wciuser +"')." ); 
   	   }
   }
   
   ///Called when a connection is closed (server).
   virtual void onRemoveConnection( pqxx::connection &con )
   {
   	try {
   	      pqxx::work x( con, "WciBeginEndHelper" );
   	      
   	      x.exec("SELECT wci.end();");
   	      x.commit();
   	      //cerr << "DTOR: DEBUG: WciBeginEndHelper!" << endl;
   	   }
   	   catch( ... ) {
   	      //Do nothing
   	   }
   }
   
   ///Called every time on DbConnectionPool::newConnection.
   virtual void onNewConnection( pqxx::connection &con ){};
   
   /**
    * Called every time on a DbConnectionPtr when the connection is put back to the pool
    * ie. the referance count is zero.
    */
   virtual void onCloseConnection( pqxx::connection &con ){};
};

}


namespace wdb2ts {

void
DbManager::
disable()
{
   for( std::map<std::string, miutil::pgpool::DbConnectionPoolPtr>::iterator it = pools_.begin();
        it != pools_.end(); ++it )
      it->second->disable();
}

DbManager::
DbManager( const miutil::pgpool::DbDefList     &dbSetup )
   : dbSetup_( dbSetup )
    
{
   miutil::pgpool::DbDef dbDef;
   //Create a pool for each of the database definitions.
   for( CIDbDefList it = dbSetup_.begin() ; it != dbSetup_.end(); ++it ) {
	   dbDef = it->second;
      if( dbDef.defaultDbDef() )
         defaultDbId_ = it->first;

      if( dbDef.maxpoolsize() <= 0 )
    	  dbDef.maxpoolsize( 30 );

      if( dbDef.usecount() <= 0 )
    	  dbDef.usecount( 100 );
      
      if( dbDef.minpoolsize() <= 0 )
        dbDef.minpoolsize( 0 );

      cerr << " Creating pool: <" << it->first << "> " << endl
            <<"       " << dbDef << endl;
      pools_[it->first] = miutil::pgpool::DbConnectionPoolPtr( 
      		              		new miutil::pgpool::DbConnectionPool( dbDef, new ConExtra( it->second.wciuser() ) )
      		              );
   }

}   
      
miutil::pgpool::DbConnectionPtr 
DbManager::
newConnection(const std::string &dbid, int timeoutInMillis )
{
   string id( dbid );
   
   //boost::mutex::scoped_lock lock( mutex );
   
   if( id.empty() )
      id = defaultDbId();
   
   if( id.empty() )
      throw logic_error("wdb2ts::DbManager::newConnection: No database id!");
   
   std::map<std::string, miutil::pgpool::DbConnectionPoolPtr>::iterator it = pools_.find( id );
   
   if( it == pools_.end() )
      throw logic_error( "wdb2ts::DbManager::newConnection: No database definition for database id <" + id + ">!");
   
   try {
      return it->second->newConnection( timeoutInMillis );
   }
   catch( const miutil::pgpool::DbConnectionPoolMaxUseEx &ex ) {
      throw;
   }
   catch( const miutil::pgpool::DbConnectionPoolCreateEx &ex ) {
      throw;
         }
   catch( const miutil::pgpool::DbConnectionException &ex ) {
      throw logic_error( ex.what() );
   }
   catch( const logic_error &ex ) {
      throw;
   }
   catch( ... ) {
      throw logic_error( "wdb2ts::DbManager::newConnection: Unknown exception!" );
   }
}

WciConnectionPtr
DbManager::
newWciConnection(const std::string &dbid, int timeoutInMilliSeconds  )
{
	string id( dbid );
	
	if( id.empty() )
		id = defaultDbId();
	   
	if( id.empty() )
		throw logic_error("wdb2ts::DbManager::newWciConnection: No database id!");
	
	CIDbDefList it = dbSetup_.find( id );
	
	if( it == dbSetup_.end() )
	   throw logic_error( "wdb2ts::DbManager::newWciConnection: No database definition for database id <" + id + ">!");
	 
	string wciuser = it->second.wciuser();
	
	if( wciuser.empty() )
		throw logic_error( "wdb2ts::DbManager::newWciConnection: Missing wciuser id for database id <" + id + ">!");
		
	return WciConnectionPtr( new WciConnection( newConnection( dbid, timeoutInMilliSeconds ), wciuser ) );
}


}
