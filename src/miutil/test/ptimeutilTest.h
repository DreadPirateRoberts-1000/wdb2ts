#ifndef __PTIMEUTILTEST_H__
#define __PTIMEUTILTEST_H__

#include <cppunit/extensions/HelperMacros.h>

class PtimeUtilTest : public CppUnit::TestFixture
{
   CPPUNIT_TEST_SUITE( PtimeUtilTest );
   CPPUNIT_TEST( testPtimeFromIsoString );
   CPPUNIT_TEST( testRFC1123 );
   CPPUNIT_TEST( geologicalLocalTime );
   CPPUNIT_TEST( enableTimePeriod );
   CPPUNIT_TEST( testToBeaufort );
   CPPUNIT_TEST_SUITE_END();

   protected:
      void testPtimeFromIsoString();
      void testRFC1123();
      void geologicalLocalTime();
      void enableTimePeriod();
      void testToBeaufort();
      
   public:
      void setUp();
      void tearDown(); 

};





#endif
