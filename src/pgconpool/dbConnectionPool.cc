/*
  Diana - A Free Meteorological Visualisation Tool

  $Id: dbConnectionPool.cc 3807 2012-03-29 11:12:47Z borgem $

  Copyright (C) 2006 met.no

  Contact information:
  Norwegian Meteorological Institute
  Box 43 Blindern
  0313 OSLO
  NORWAY
  email: diana@met.no
  
  This file is part of Diana

  Diana is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Diana is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with Diana; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <iostream>
#include <boost/version.hpp>

#if BOOST_VERSION < 103500
#include <boost/thread/xtime.hpp>
#endif

#include "dbConnectionPool.h"
#include "dbConnectionHelper.h"


using namespace std;
using namespace pqxx;


void 
miutil::pgpool::
ConnectionExtra::
onCreateConnection( pqxx::connection &con )
{
	//NOOP
}
      

void 
miutil::pgpool::
ConnectionExtra::
onRemoveConnection( pqxx::connection &con )
{
	//NOOP
}

void
miutil::pgpool::
ConnectionExtra::
onNewConnection( pqxx::connection &con )
{
	//NOOP
}
      
void 
miutil::pgpool::
ConnectionExtra::
onCloseConnection( pqxx::connection &con )
{
	//NOOP
}


miutil::pgpool::
DbConnectionPool::
DbConnectionPool(const std::string &connect, ConnectionExtra *conExtra_ )
  :maxpoolsize_(1), minpoolsize_(0), maxUseCount_(1), connect_(connect), isEnabled_( true )
{	
	if( ! conExtra_ )
		conExtra = new ConnectionExtra();
	else
		conExtra = conExtra_;

}

miutil::pgpool::
DbConnectionPool::
DbConnectionPool( const DbDef &dbdef,
                  ConnectionExtra *conExtra_)
: isEnabled_( true )
{
	ostringstream ost;

	if( ! conExtra_ )
		conExtra = new ConnectionExtra();
	else
		conExtra = conExtra_;

	maxUseCount_ = dbdef.usecount();
	maxpoolsize_ = dbdef.maxpoolsize();
	minpoolsize_ = dbdef.minpoolsize();

	if( minpoolsize_ > maxpoolsize_ ) {
		int tmp=maxpoolsize_;
		maxpoolsize_ = minpoolsize_;
		minpoolsize_ = tmp;
	}

	ost << "dbname=" << dbdef.dbname();

	if( ! dbdef.user().empty() )
		ost << " user=" << dbdef.user();

	if( ! dbdef.password().empty() )
		ost << " password=" << dbdef.password();

	if( ! dbdef.host().empty() )
		ost << " host=" << dbdef.host();

	if(  dbdef.port() > 0 )
		ost << " port=" << dbdef.port();

	connect_ = ost.str();

	createMinpoolsize();
}

miutil::pgpool::
DbConnectionPool::
DbConnectionPool(int maxConnections, 
                 const std::string &connect, 
                 int maxUseCount,
                 ConnectionExtra *conExtra_)
         : maxpoolsize_(maxConnections),
           minpoolsize_(0),
           maxUseCount_(maxUseCount),
           connect_(connect),
           isEnabled_( true )
{
	if( ! conExtra_ )
		conExtra = new ConnectionExtra();
	else
		conExtra = conExtra_;
}

void
miutil::pgpool::
DbConnectionPool::
disable()
{
   boost::mutex::scoped_lock lock(mutex);

   if( ! isEnabled_ )
      return;

   isEnabled_ = false;

   std::list<internal::DbConnectionHelper*>::iterator it = pool.begin();
   while( it != pool.end() ) {
      if ( ! (*it)->inUse() ) {
         std::list<internal::DbConnectionHelper*>::iterator tmpIt = it;
         ++it;
         conExtra->onRemoveConnection( (*tmpIt)->connection_ );
         delete *tmpIt;
         pool.erase( tmpIt );
      } else {
         ++it;
      }
   }

   cond.notify_all();
}


void
miutil::pgpool::
DbConnectionPool::
enable()
{
   boost::mutex::scoped_lock lock(mutex);

   isEnabled_ = true;
   cond.notify_all();
}

bool
miutil::pgpool::
DbConnectionPool::
isEnabled()const
{
   boost::mutex::scoped_lock lock( mutex);

   return isEnabled_;
}

void 
miutil::pgpool::
DbConnectionPool::
release(internal::DbConnectionHelper *con)
{
   boost::mutex::scoped_lock lock(mutex);
   
   conExtra->onCloseConnection( con->connection_ );
   
   if ( con->useCount_ == 0 || ! isEnabled_ ) {
      //cerr << "DbConnectionPool::release: Connection count==0, disconect and delete the connection!\n";
      
      for ( std::list<internal::DbConnectionHelper*>::iterator it = pool.begin();
            it!=pool.end();
            ++it)
         if ( *it == con ) {
         	conExtra->onRemoveConnection( con->connection_ );
            delete *it;
            pool.erase( it );

            if( isEnabled_ && pool.size() < minpoolsize_ ) {
            	try {
            		createConnection();
            		clog << "DbConnectionPool::release: poolsize<minpoolsize: Connection recreated." << endl;
            	}
            	catch( std::exception &ex ) {
            		cerr << "EXCEPTION: DbConnectionPool::release: poolsize<minpoolsize: recreate connection failed. <"
            			 << ex.what() << ">" << endl;

            	}
            }

            cond.notify_one();
            return;
         }
         
      clog << "DbConnectionPool::release: Connection NOT in the connection pool!" << endl;
   } else {
        con->inUse( false );
        cond.notify_one();
   }
}

miutil::pgpool::
DbConnectionPool::                
~DbConnectionPool()
{
//   cerr << "DbConnectionPool: DTOR " << endl;
   for ( std::list<internal::DbConnectionHelper*>::iterator it = pool.begin();
         it!=pool.end();
         ++it)
      if ( (*it)->inUse() ) {
         cerr << "FATAL: DbConnectionPool::DTOR: Connection in use.\n";
         delete *it;
      }
   
   delete conExtra;
}

miutil::pgpool::internal::DbConnectionHelper*
miutil::pgpool::
DbConnectionPool::
createConnection()
{

   if ( pool.size() >= maxpoolsize_ )
      return 0;

	if( ! isEnabled_ )
	   throw DbConnectionDisabledEx();

	internal::DbConnectionHelper *con;

	try {
		con=new internal::DbConnectionHelper( maxUseCount_, connect_ );
		conExtra->onCreateConnection( con->connection_ );
	}
	catch ( std::exception &ex ) {
		throw DbConnectionPoolCreateEx(
				string( "EXCEPTION: DbConnectionPool::newConnection: ") + string(ex.what())
		);
	}
	catch ( ... ) {
	   throw DbConnectionPoolCreateEx("EXCEPTION: DbConnectionPool::newConnection.");
	}

	pool.push_back( con );

	return con;
}

miutil::pgpool::internal::DbConnectionHelper*
miutil::pgpool::
DbConnectionPool::
findOrCreateConnection()
{
   try {
      for ( std::list<internal::DbConnectionHelper*>::iterator it = pool.begin();
            it!=pool.end();
            ++it) {
         if ( ! (*it)->inUse() ) {
            conExtra->onNewConnection( (*it)->connection_ );
            (*it)->inUse( true );
            return *it;
         }
      }

      internal::DbConnectionHelper *con = createConnection();

      if( con ) {
         conExtra->onNewConnection( con->connection_ );
         con->inUse(true);
      }

      return con;
   }
   catch( ... ) {
      return 0;
   }
}


void
miutil::pgpool::
DbConnectionPool::
createMinpoolsize() {
	for( std::list<internal::DbConnectionHelper*>::size_type i = 0;
	     i < minpoolsize_; ++i ) {
    	try {
    		if( ! createConnection() ) //Is maxPoolSize reached.
    		   break;
    	}
    	catch( std::exception &ex ) {
    		cerr << "EXCEPTION: DbConnectionPool::createMinpoolsize: init pool to minpoolsize: " << minpoolsize_
    			 << " failed! <" << ex.what() << ">" << endl;
    		return;
    	}
	}

	clog << "Created a DB connection pool with " << pool.size() << " connections." << endl;
}


miutil::pgpool::DbConnectionPtr 
miutil::pgpool::
DbConnectionPool::
newConnection( unsigned int timeoutInMillisecond )
{
   boost::mutex::scoped_lock lock(mutex);
   
   if( ! isEnabled_ )
      throw DbConnectionDisabledEx();

   internal::DbConnectionHelper *con = findOrCreateConnection();
   
   if( con )
      return DbConnectionPtr( new DbConnection( con, this ) );
   
   if( timeoutInMillisecond == 0 && pool.size() >= maxpoolsize_ )
      throw DbConnectionPoolMaxUseEx();

#if BOOST_VERSION < 103500
   boost::xtime waitUntil;
   //Round up to the nearest second if timeoutInMillisecond is not a multiple of 1000.
   int sec = (timeoutInMillisecond/1000) + (((timeoutInMillisecond % 1000) == 0)?0:1);
   boost::xtime_get( &waitUntil, boost::TIME_UTC );
   waitUntil.sec += sec;

   if( ! cond.timed_wait( lock, waitUntil ) )
      throw DbConnectionPoolMaxUseEx();
#else
   if( ! cond.timed_wait( lock, boost::posix_time::milliseconds( timeoutInMillisecond ) ) )
      throw DbConnectionPoolMaxUseEx();
#endif

   con = findOrCreateConnection();

   if( ! con )
      throw DbConnectionPoolMaxUseEx();
   else
      return DbConnectionPtr( new DbConnection( con, this ) );
}
         
