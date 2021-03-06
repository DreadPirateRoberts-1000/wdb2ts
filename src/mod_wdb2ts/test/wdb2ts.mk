#-----------------------------------------------------------------------------
# mod_wdb2ts Unit Test Framework
#
# See: <top>/test/unit
#-----------------------------------------------------------------------------

wdb2TsUnitTest_SOURCES += \
	src/mod_wdb2ts/test/UrlParamTest.cpp \
   src/mod_wdb2ts/test/UrlParamTest.h \
   src/mod_wdb2ts/test/QueryMakerTest.cpp \
   src/mod_wdb2ts/test/QueryMakerTest.h \
   src/mod_wdb2ts/test/WciWebQueryTest.cpp \
   src/mod_wdb2ts/test/WciWebQueryTest.h \
   src/mod_wdb2ts/test/ProjectionTest.h \
   src/mod_wdb2ts/test/ProjectionTest.cpp \
   src/mod_wdb2ts/test/SymbolTest.h \
   src/mod_wdb2ts/test/SymbolTest.cpp \
   src/mod_wdb2ts/test/VariosTests.h \
   src/mod_wdb2ts/test/VariosTests.cpp\
   src/mod_wdb2ts/test/UpdateWebQueryTest.h\
   src/mod_wdb2ts/test/UpdateWebQueryTest.cpp \
   src/mod_wdb2ts/test/MetaModelTest.h \
   src/mod_wdb2ts/test/MetaModelTest.cpp \
   src/mod_wdb2ts/test/readcsv.h \
   src/mod_wdb2ts/test/readcsv.cpp 

EXTRA_DIST += \
	src/mod_wdb2ts/test/wdb2ts.mk \
	src/mod_wdb2ts/test/Makefile.am \
	src/mod_wdb2ts/test/Makefile.in

DISTCLEANFILES += src/mod_wdb2ts/test/Makefile
