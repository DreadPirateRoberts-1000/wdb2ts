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

#include <string>
#include <File.h>

using namespace std;

std::string  
dnmi::file::
File::
path()const
{
  if(name_.empty())
    return string();

  string::size_type i;
  
  i=name_.find_last_of("/");
  
  if(i==string::npos)
    return string();
  
  string p=name_.substr(0, i);

  while(!p.empty() && (p[p.length()-1]=='/' || p[p.length()-1]==' '))
    p.erase(p.length()-1);

  return p;

}

std::string  
dnmi::file::
File::
file()const
{
  if(name_.empty())
    return string();

  string::size_type i;
  
  i=name_.find_last_of("/");
  
  if(i==string::npos)
    return name_;
  
  string p=name_.substr(i+1);

  while(!p.empty() && p[p.length()-1]==' ')
    p.erase(p.length()-1);

  return p;
}
