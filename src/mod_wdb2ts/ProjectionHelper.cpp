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


#include <float.h>
#include <math.h>
#include <ProjectionHelper.h>
#include <splitstr.h>
#include <list>
#include <milib/milib.h>
#include <wdb2TsApp.h>
#include <DbManager.h>
#include <Logger4cpp.h>
#include <boost/regex.hpp>
#include <sstream>

namespace {
	float getProjFloat( const std::string &projdef, const boost::regex &what ) {
		using namespace boost;
		smatch match;
		float val;

		if( ! regex_search( projdef, match, what ) )
			return FLT_MAX;

		std::string sval = match[1];

		if( sscanf( sval.c_str(), " %f", &val) != 1 )
			return FLT_MAX;

		return val;
	}

}

namespace wdb2ts {

using namespace std;


MiProjection::
MiProjection()
	:gridType( undefined_projection )
{
}

MiProjection::
MiProjection( GridSpec gridSpec, ProjectionType pt )
{
	set( gridSpec, pt );
}

MiProjection::
MiProjection( const MiProjection &proj )
{
	for( int i = 0; i < 6; ++i )
		gridSpec[i] = proj.gridSpec[i];
	
	gridType = proj.gridType;
}

MiProjection&
MiProjection::
operator=( const MiProjection &rhs )
{
	if( this != &rhs ) {
		for( int i = 0; i < 6; ++i )
			gridSpec[i] = rhs.gridSpec[i];
		
		gridType = rhs.gridType;
	}
	
	return *this;
}

void 
MiProjection::
makeGeographic()
{
	GridSpec gs = { 1.0, 1.0, 1.0, 1.0, 0.0, 0.0 };
	
	set( gs, geographic );
}

/*
 * The initialization is a copy from the function
 * Projection::set_mi_gridspec(const int gt, const float gs[speclen]) 
 * in the file diField/diProjection.cc
 */
void 
MiProjection::
set( GridSpec gridSpec_, ProjectionType pt  )
{
	gridType = pt;
	GridSpec gridspecstd;
	/*  
	  Fix gridspec from fortran indexing (from 1) to C/C++ indexing (from 0).
	  To be used in fortran routines xyconvert and uvconvert,
	  gridspecstd must be used in fortran routine mapfield
	  and when writing a field to a field file.
	 */
	for( int i=0; i<6; i++ ) {
		gridspecstd[i]= gridSpec_[i];
		gridSpec[i] =   gridSpec_[i];
	}

	/*
	    projection type 1 : polarstereographic at 60 degrees north !!!
	    (mapareas defined in setup without setting true latitude to 60.,
	    no problem for fields when using gridpar)
	 */
	if( gridType == polarstereographic_60 ) {
		gridspecstd[4]= 60.;
		gridSpec[4]=    60.;
	}

	/*
	    call movegrid on grid specification if not undefined projection:
	    Offsets "gridspecstd" with (1,1) and output result in "gridspec"
	 */
	if( gridType != undefined_projection ) {
		int ierror= 1;
		::movegrid( gridType, gridspecstd, 1.0, 1.0, gridSpec, &ierror);
	    
		if (ierror){
			WEBFW_USE_LOGGER( "handler" );
	      	WEBFW_LOG_ERROR( "movegrid error : " << ierror << " (in MiProjection::set)" );
		}
	}
}

bool 
MiProjection::
xyconvert( const MiProjection &proj, float &latitude, float &longitude )const
{
	int error;
	WEBFW_USE_LOGGER( "handler" );


	if( gridType == undefined_projection || proj.gridType == undefined_projection ) {
		WEBFW_LOG_ERROR("MiProjection::xyconvert: undefined projection.");
		return false;
	}
	/*
	 extern void xyconvert(int npos, float *x, float *y,
				int igtypa, float *ga,
				int igtypr, float *gr, int *ierror);
	 */
	::xyconvert( 1, &longitude, &latitude, 
				  static_cast<int>(proj.gridType), const_cast<float*>(proj.gridSpec), 
				  static_cast<int>(gridType), const_cast<float*>(gridSpec), &error);
	
	if( error != 0 ) {
		WEBFW_LOG_ERROR( "MiProjection::xyconvert: Conversion error. Error code: " << error << "." );
		return false;
	}
	
	return true;
}

bool 
MiProjection::
uvconvert( const MiProjection &proj, float latitude, float longitude, float &u, float &v )const
{
	int error;
	WEBFW_USE_LOGGER( "handler" );

	if( gridType == undefined_projection || proj.gridType == undefined_projection ) {
		WEBFW_LOG_DEBUG( "MiProjection::uvconvert: undefined projection." );
		return false;
	}
	
	/*
	    extern void uvconvert(int npos, float *xr, float *yr,
			float *u, float *v,
			int igtypa, float *ga,
			int igtypr, float *gr, float udef, int *ierror);
	 */ 
	
	::uvconvert( 1, &longitude, &latitude, &u, &v,
			       static_cast<int>(proj.gridType), const_cast<float*>(proj.gridSpec),
			       static_cast<int>(gridType), const_cast<float*>(gridSpec), FLT_MAX, &error);
	
	if( error != 0 ) {
		WEBFW_LOG_ERROR( "MiProjection::uvconvert: Conversion error. Error code: " << error << "." );
		return false;
	}
		
	return true;
	
}


ProjectionHelper::
ProjectionHelper()
	: isInitialized(false), wciProtocol( 1 )
{
	geographic.makeGeographic();
}

bool 
ProjectionHelper::
loadFromDBWciProtocol_1( pqxx::connection& con, 
		                   const wdb2ts::config::ActionParam &params 
		                )
{
	map<string, list<string> > places;
	map<string, list<string> >::iterator itPlace;
	list<string>::iterator itProvider;
	WEBFW_USE_LOGGER( "handler" );

	if( isInitialized )
		return true;
	
	isInitialized = true;
	
	wdb2ts::config::ActionParam::const_iterator it=params.find("wdb.projection");
		
	if( it == params.end() ) {
		WEBFW_LOG_WARN( "No wdb.projection specification!" );
		return true;
	}
	
	vector<string> vec = miutil::splitstr(it->second.asString(), ';');
	
	if( vec.empty() ) {
		WEBFW_LOG_WARN( "wdb.projection specification. No valid values!" );
		return false;
	}
	
	for( vector<string>::size_type i=0; i<vec.size(); ++i ) {
		vector<string> vals = miutil::splitstr( vec[i], '=');
		
		if( vals.size() != 2 ) {
			WEBFW_LOG_WARN( "wdb.projection specification. value <" << vec[i] << "> wrong format!");
			continue;
		}
		
		places[ vals[1] ].push_back( vals[0] );
	}

	try {
		pqxx::work work( con, "WciPlaceSpecification");
		pqxx::result  res = work.exec( "SELECT * FROM wci.placespecification()" );
		
		MiProjection::GridSpec gs;
		MiProjection::ProjectionType pType;
		int nCols;
		int nRows;

		for( pqxx::result::const_iterator row=res.begin(); row != res.end(); ++row ){
			itPlace = places.find( row.at("placename").c_str() );
			
			if( itPlace == places.end() || itPlace->second.empty() )
				continue;
			
			nCols = row["inumber"].as<int>();
			nRows = row["jnumber"].as<int>();
			gs[0] = row["startlongitude"].as<float>();
			gs[1] = row["startlatitude"].as<float>();
			gs[2] = row["jincrement"].as<float>();
			gs[3] = row["iincrement"].as<float>();
			
			// The SRIDs are hardcoded into the code, instead of trying to decode
			// the PROJ string.
			switch ( row["originalsrid"].as<int>() ) {
			case 50000:
			case 50007:
				//From comments in milib shall gs allways be set to
				//this value for geographic projection.
				gs[0] = 1.0;
				gs[1] = 1.0;
				gs[2] = 1.0;
				gs[3] = 1.0;
				gs[4] = 0.0;
				gs[5] = 0.0;
				pType = MiProjection::geographic;
				break;
			case 50001: // Hirlam 10     
				gs[4] = -40.0;
				gs[5] = 68.0;
				pType = MiProjection::spherical_rotated;
				break;
			case 50002: // Hirlam 20
				gs[4] = 0.0;
				gs[5] = 65.0;
				pType = MiProjection::spherical_rotated;
				break;
			case 50003: // PROFET
				gs[4] = -24.0;
				gs[5] = 66.5;
				pType = MiProjection::spherical_rotated;
				break;
			case 50004:
				//TODO: This is only valid for Nordic4km?
				gs[0] = (0-gs[0])/gs[3] ; //poleGridX = (0 - startX) / iIncrement
				gs[1] = (0-gs[1])/gs[2];  //poleGridY = (0 - startY) / jIncrement
				gs[3] = 58;
				gs[4] = 60;
				gs[5] = 0.0;
				pType = MiProjection::polarstereographic;
				break;
			default:
				gs[4] = 0.0;
				gs[5] = 0.0;
				pType = MiProjection::spherical_rotated;
				break;
			}
		/*
		WEBFW_LOG_DEBUG( row.at("placename").c_str() << "i#: " << row.at("inumber").as<int>()
			     << " j#: " << row.at("jnumber").as<int>() 
			     << " iincr: " << row.at("iincrement").as<float>()
			     << " jincr: " << row.at("jincrement").as<float>()
			     << " startlon: " << row.at("startlongitude").as<float>()
			     << " startlat: " << row.at("startlatitude").as<float>()	
			     << " srid: " << row.at("originalsrid").as<int>()
			     << " proj: " << row.at("projdefinition").c_str() );
		*/
			
			for( itProvider = itPlace->second.begin(); itProvider != itPlace->second.end(); ++itProvider ) {
				projectionMap[ *itProvider ] = MiProjection( gs, pType );
				WEBFW_LOG_INFO( "wdb.projection added: " << *itProvider  << ": " << projectionMap[ *itProvider ] );
			}	
			
		}
	}
	catch( std::exception &ex ) {
		WEBFW_LOG_ERROR( "ProjectionHelper::loadFromDB: " << ex.what() );
		return false;
	}
	catch( ... ) {
		WEBFW_LOG_ERROR( "EXCEPTION: ProjectionHelper::loadFromDB: UNKNOWN reason!" );
		return false;
	}
	
	return true;
}

bool 
ProjectionHelper::
loadFromDBWciProtocol_2( pqxx::connection& con, 
		                   const wdb2ts::config::ActionParam &params
		                 )
{
	WEBFW_USE_LOGGER( "handler" );

	try {
		pqxx::work work( con, "WciPlaceSpecification");
		pqxx::result  res = work.exec( "SELECT * FROM wci.placespecification()" );
	
		placenameMap.clear();
		
		MiProjection::GridSpec gs;
		MiProjection::ProjectionType pType;
		int nCols;
		int nRows;

		for( pqxx::result::const_iterator row=res.begin(); row != res.end(); ++row )
		{
			nCols = row["inumber"].as<int>();
			nRows = row["jnumber"].as<int>();
			gs[0] = row["startlongitude"].as<float>();
			gs[1] = row["startlatitude"].as<float>();
			gs[2] = row["jincrement"].as<float>();
			gs[3] = row["iincrement"].as<float>();
			
			// The SRIDs are hardcoded into the code, instead of trying to decode
			// the PROJ string.
			switch ( row["originalsrid"].as<int>() ) {
			case 50000:
			case 50007:
				//From comments in milib shall gs allways be set to
				//this value for geographic projection.
				gs[0] = 1.0;
				gs[1] = 1.0;
				gs[2] = 1.0;
				gs[3] = 1.0;
				gs[4] = 0.0;
				gs[5] = 0.0;
				pType = MiProjection::geographic;
				break;
			case 50001: // Hirlam 10     
				gs[4] = -40.0;
				gs[5] = 68.0;
				pType = MiProjection::spherical_rotated;
				break;
			case 50002: // Hirlam 20
				gs[4] = 0.0;
				gs[5] = 65.0;
				pType = MiProjection::spherical_rotated;
				break;
			case 50003: // PROFET
				gs[4] = -24.0;
				gs[5] = 66.5;
				pType = MiProjection::spherical_rotated;
				break;
			case 50004: //Sea and wave models
				gs[0] = (0-gs[0])/gs[3] ; //poleGridX = (0 - startX) / iIncrement
				gs[1] = (0-gs[1])/gs[2];  //poleGridY = (0 - startY) / jIncrement
				gs[3] = 58;
				gs[4] = 60;
				gs[5] = 0.0;
				pType = MiProjection::polarstereographic;
				break;
			case 50005: //Sea and wave models
				gs[0] = (0-gs[0])/gs[3] ; //poleGridX = (0 - startX) / iIncrement
				gs[1] = (0-gs[1])/gs[2];  //poleGridY = (0 - startY) / jIncrement
				gs[3] = 24;
				gs[4] = 60;
				gs[5] = 0.0;
				pType = MiProjection::polarstereographic;
				break;
			default:
				pType =  MiProjection::undefined_projection;
				/*
				gs[4] = 0.0;
				gs[5] = 0.0;
				pType = MiProjection::spherical_rotated;
				*/
				break;
			}
		/*
		WEBFW_LOG_DEBUG( row.at("placename").c_str() << "i#: " << row.at("inumber").as<int>()
			     << " j#: " << row.at("jnumber").as<int>() 
			     << " iincr: " << row.at("iincrement").as<float>()
			     << " jincr: " << row.at("jincrement").as<float>()
			     << " startlon: " << row.at("startlongitude").as<float>()
			     << " startlat: " << row.at("startlatitude").as<float>()	
			     << " srid: " << row.at("originalsrid").as<int>()
			     << " proj: " << row.at("projdefinition").c_str() );
		*/

			placenameMap[row.at("placename").c_str()] = MiProjection( gs, pType );
		}

		ostringstream msg;
		for( ProjectionMap::const_iterator it=placenameMap.begin();
			 it != placenameMap.end(); ++it ) {
			 msg.str("");
			 msg << "Projection (P2): " << it->first << "  " << it->second;
			 WEBFW_LOG_DEBUG( msg.str() );
		}
	}
	catch( std::exception &ex ) {
		WEBFW_LOG_ERROR( "EXCEPTION: ProjectionHelper::loadFromDB: " << ex.what() );
		return false;
	}
	catch( ... ) {
		WEBFW_LOG_ERROR( "EXCEPTION: ProjectionHelper::loadFromDB: UNKNOWN reason!" );
		return false;
	}
	
	return true;
}

bool
ProjectionHelper::
loadFromDBWciProtocol_4( pqxx::connection& con,
		                   const wdb2ts::config::ActionParam &params
		                 )
{
	using namespace boost;

	WEBFW_USE_LOGGER( "handler" );
	const string reFloat = "[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?";
	regex reproj( "\\+proj *= *(\\w+)" );
	regex relat_0( "\\+lat_0 *= *("+ reFloat + ")" );
	regex relon_0( "\\+lon_0 *= *("+ reFloat + ")" );
	regex relat_ts( "\\+lat_ts *= *("+ reFloat + ")" );
	regex reo_lat_p( "\\+o_lat_p *= *("+ reFloat + ")" );
	string proj;
	ostringstream msg;

	try {

		WEBFW_LOG_DEBUG( "ProjectionHelper::P4: SELECT * FROM wci.info( NULL, NULL::wci.inforegulargrid )" );
		pqxx::work work( con, "wci.inforegulargrid");
		pqxx::result  res = work.exec( "SELECT * FROM wci.info( NULL, NULL::wci.inforegulargrid )" );

		placenameMap.clear();

		MiProjection::GridSpec gs;
		MiProjection::ProjectionType pType;
		int nCols;
		int nRows;
		smatch match;

		for( pqxx::result::const_iterator row=res.begin(); row != res.end(); ++row )
		{
			nCols = row["numberx"].as<int>();
			nRows = row["numbery"].as<int>();
			gs[0] = row["startx"].as<float>();
			gs[1] = row["starty"].as<float>();
			gs[2] = row["incrementy"].as<float>();
			gs[3] = row["incrementx"].as<float>();
			proj = row["projdefinition"].c_str();

			if( ! regex_search( proj, match, reproj ) ) {
				WEBFW_LOG_ERROR( "No projection in the proj string from wdb <"+proj+">." );
				continue;
			} else if( match[1] == "longlat" ) {
				pType = MiProjection::geographic;
			} else if( match[1] == "stere" ) {
				pType = MiProjection::polarstereographic;
			} else if( match[1] == "ob_tran" ) {
				pType = MiProjection::spherical_rotated;
			} else {
				pType = MiProjection::undefined_projection;
				WEBFW_LOG_WARN( "Unsupported projection <"+match[1] +">. placename: " + row["placename"].c_str() );
				continue;
			}

			switch( pType ) {
			case MiProjection::geographic: {
//				WEBFW_LOG_DEBUG( "ProjectionHelper::P4: geographic: "+proj);
				//From comments in milib shall gs allways be set to
				//this value for geographic projection.
				gs[0] = 1.0;
				gs[1] = 1.0;
				gs[2] = 1.0;
				gs[3] = 1.0;
				gs[4] = 0.0;
				gs[5] = 0.0;
				pType = MiProjection::geographic;
			}
			break;

			case MiProjection::polarstereographic: {
				float lat_0 = getProjFloat( proj, relat_0 );
				float lon_0 = getProjFloat( proj, relon_0 );
				float lat_ts = getProjFloat( proj, relat_ts );

				if( lat_0 == FLT_MAX || lon_0==FLT_MAX || lat_ts == FLT_MAX ) {
					WEBFW_LOG_ERROR( "Polarstereographic projection missing one of: +lat_0, +lon_0 or +lat_ts <"
							         + proj +">.");
					continue;
				}
//				msg.str("");
//				msg << "ProjectionHelper::P4: polarstereographic: lat_0: " << lat_0 << " lon_0: " << lon_0 << " lat_ts: " << lat_ts <<
//						" (" << proj << ")";
//				WEBFW_LOG_DEBUG( msg.str() );

				gs[0] = ( 0 - gs[0] ) / gs[3]; //poleGridX = (0 - startX) / iIncrement
				gs[1] = ( 0 - gs[1] ) / gs[2];  //poleGridY = (0 - startY) / jIncrement
				gs[3] = lon_0;
				gs[4] = lat_ts;
				gs[5] = 90-lat_0;
			}
			break;

			case MiProjection::spherical_rotated: {
				float lon_0 = getProjFloat( proj, relon_0 );
				float o_lat_p = getProjFloat( proj, reo_lat_p );

				if( lon_0==FLT_MAX || o_lat_p == FLT_MAX ) {
					WEBFW_LOG_ERROR( "Spherical Rotated projection missing one of: +lon_0 or +o_lat_p <"
							+ proj +">.");
					continue;
				}

//				msg.str("");
//				msg << "ProjectionHelper::P4: spherical_rotated: lon_0: " << lon_0 << " o_lat_p: " << o_lat_p <<
//				       " (" << proj << ")";
//				WEBFW_LOG_DEBUG( msg.str() );
				gs[4] = lon_0;
				gs[5] = 90 - o_lat_p;
			}
			break;

			default:
				WEBFW_LOG_WARN( "Unsupported projection <"+match[1] +">. placename: " + row["placename"].c_str() );
				continue;

			}

			if( pType ==  MiProjection::undefined_projection )
				continue;

			placenameMap[row.at("placename").c_str()] = MiProjection( gs, pType );
		}

		for( ProjectionMap::const_iterator it=placenameMap.begin();
		     it != placenameMap.end(); ++it ) {
			msg.str("");
			msg << "Projection (P4): " << it->first << "  " << it->second;
			WEBFW_LOG_DEBUG( msg.str() );
		}
	}
	catch( std::exception &ex ) {
		WEBFW_LOG_ERROR( "EXCEPTION: ProjectionHelper::loadFromDB (protocol 4): " << ex.what() );
		return false;
	}
	catch( ... ) {
		WEBFW_LOG_ERROR( "EXCEPTION: ProjectionHelper::loadFromDB (protocol 4): UNKNOWN reason!" );
		return false;
	}

	return true;
}

bool 
ProjectionHelper::
loadFromDB( pqxx::connection& con, 
		      const wdb2ts::config::ActionParam &params,
		      int wciProtocol_ )
{
	boost::mutex::scoped_lock lock( mutex );
	
	wciProtocol = wciProtocol_;
	
	if( wciProtocol > 1  && wciProtocol <= 3 )
		return loadFromDBWciProtocol_2( con, params );
	
	if( wciProtocol >= 4 )
		return loadFromDBWciProtocol_4( con, params );


	return loadFromDBWciProtocol_1( con, params );
}


ProjectionHelper::ProjectionMap::const_iterator
ProjectionHelper::
findProjection( const std::string &provider ) 
{
	ProjectionMap::const_iterator it;
	WEBFW_USE_LOGGER( "handler" );

#if 0
	WEBFW_LOG_DEBUG( "ProjectionHelper::findProjection: protocol: " << wciProtocol );
	WEBFW_LOG_DEBUG( "ProjectionHelper::findProjection: provider: " << provider );
	for( it = projectionMap.begin(); it != projectionMap.end(); ++it ) {
		WEBFW_LOG_DEBUG( "ProjectionHelper::findProjection: " << it->first << ": " << it->second );
	}
#endif
	
	it = projectionMap.find( provider );
	
	if( it != projectionMap.end() )
		return it;
	
	if( wciProtocol == 1 )
		return projectionMap.end();
	
	
#if 0	
	for( it = placenameMap.begin(); it != placenameMap.end(); ++it ) {
		WEBFW_LOG_DEBUG( "ProjectionHelper::findProjection: placenameMap: " << it->first << ": " << it->second );
	}
#endif		
	
	//The provider may be on the form 'provider [placename]'. 
	//Search the placename map for the place name and add it to the 
	//projectionMap.
	ProviderItem pi=ProviderList::decodeItem( provider );

#if 0
	WEBFW_LOG_DEBUG( "ProjectionHelper::findProjection: placenameMap: pi.placename: " << pi.placename );
	WEBFW_LOG_DEBUG( "ProjectionHelper::findProjection: placenameMap: pi.provider:  " << pi.provider );
	WEBFW_LOG_DEBUG( "ProjectionHelper::findProjection: placenameMap: pi.providerWithPlacename:  " << pi.providerWithPlacename() );
#endif
	
	if( pi.placename.empty() )
		return projectionMap.end();
	
	it = placenameMap.find( pi.placename );
	
	if( it == placenameMap.end() )
		return projectionMap.end();
	
	WEBFW_LOG_DEBUG( "ProjectionHelper: Added provider <" << pi.providerWithPlacename() << "> to the projection cache! " << it->second );
	
	projectionMap[ pi.providerWithPlacename() ] = it->second;
	
	return projectionMap.find( provider );
}
	



bool 
ProjectionHelper::
convertToDirectionAndLength( const std::string &provider, 
		                       float latitude, float longitude, 
								     float u, float v, 
								     float &direction, float &length, bool turn )const
{
	if( u == FLT_MAX || v == FLT_MAX )
		return false;
	
	WEBFW_USE_LOGGER( "handler" );

	{ //Mutex protected scope.
		boost::mutex::scoped_lock lock( const_cast<ProjectionHelper*>(this)->mutex );
	
		ProjectionMap::const_iterator it = 
				const_cast<ProjectionHelper*>(this)->findProjection( provider );
		
		if( it != projectionMap.end() ) 
		{
			if( it->second.getProjectionType() == MiProjection::undefined_projection ) {
				WEBFW_LOG_WARN( "ProjectionHelper::convertToDirectionAndLength: Projection definition for provider <" << provider << "> is 'undefined_projection'!" );
				return false;
			}
			
			if( ! geographic.uvconvert( it->second, latitude, longitude, u, v ) ){
				WEBFW_LOG_ERROR( "ProjectionHelper::convertToDirectionAndLength: failed!" );
				return false;
			}
		
			//WEBFW_LOG_DEBUG( "ProjectionHelper::convertToDDandFF: DEBUG provider <" << provider << ">!" );
		} else {
			WEBFW_LOG_WARN( "ProjectionHelper::convertToDirectionAndLength: WARNING no Projection definition for provider <" << provider << ">!" );
		}
	} //End mutex protected scope.
	
	if( u == FLT_MAX || v == FLT_MAX ) {
		WEBFW_LOG_ERROR( "ProjectionHelper::convertToDirectionAndLength: failed u or/and v undefined!" );
		return false;
	}
		
	float deg=180./3.141592654;
	float fTurn;
	
	if( turn ) 
		fTurn = 90.0;
	else
		fTurn = 270.0;
		
	length = sqrtf(u*u + v*v);
	     
	if( length>0.0001) {
		direction = fTurn - deg*atan2( v, u );	
		if( direction > 360 ) direction -=360;
		if( direction < 0   ) direction +=360;
	} else {
		direction = 0;
	}
	
	return true;
}


bool 
configureWdbProjection( ProjectionHelper &projectionHelper,
		                  const wdb2ts::config::ActionParam &params,
		                  int wciProtocol, 
		                  const std::string &wdbDB,
		                  Wdb2TsApp *app )
{
	ProviderList providerList;
	WEBFW_USE_LOGGER( "handler" );
	
	try {
		miutil::pgpool::DbConnectionPtr con = app->newConnection( wdbDB );
		
		projectionHelper.loadFromDB( con->connection(), params, wciProtocol );
		return true;
	}
	catch( std::exception &ex ) {
		WEBFW_LOG_ERROR( "EXCEPTION: configureWdbProjection: " << ex.what() );
	}
	catch( ... ) {
		WEBFW_LOG_ERROR( "EXCEPTION: configureWdbProjection: UNKNOWN reason.");
	}
	
	return false;
}




std::ostream& 
operator<<(std::ostream &o, const MiProjection &proj )
{
	string grid;
	
	switch( proj.gridType ) {
	case MiProjection::undefined_projection:  grid="undefined_projection"; break;
	case MiProjection::polarstereographic_60: grid="polarstereographic_60"; break;
	case MiProjection::geographic:            grid="geographic"; break;
	case MiProjection::spherical_rotated:     grid="spherical_rotated"; break;
	case MiProjection::polarstereographic:    grid="polarstereographic"; break;
	case MiProjection::mercator:              grid="mercator"; break;
	case MiProjection::lambert:               grid="lambert"; break;
	default:
		grid="UNKNOWN grid type"; break;
	}
	
	o << "MiProjection[" << grid << "," 
	  << proj.gridSpec[0] << "," 
	  << proj.gridSpec[1] << "," 
	  << proj.gridSpec[2] << "," 
	  << proj.gridSpec[3] << "," 
	  << proj.gridSpec[4] << ","
	  << proj.gridSpec[5] << "]";
	return o;
}

}





