package OpenSSL::safe::installdata;

use strict;
use warnings;
use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(
    @PREFIX
    @libdir
    @BINDIR @BINDIR_REL_PREFIX
    @LIBDIR @LIBDIR_REL_PREFIX
    @INCLUDEDIR @INCLUDEDIR_REL_PREFIX
    @APPLINKDIR @APPLINKDIR_REL_PREFIX
    @ENGINESDIR @ENGINESDIR_REL_LIBDIR
    @MODULESDIR @MODULESDIR_REL_LIBDIR
    @PKGCONFIGDIR @PKGCONFIGDIR_REL_LIBDIR
    @CMAKECONFIGDIR @CMAKECONFIGDIR_REL_LIBDIR
    $COMMENT $VERSION @LDLIBS
);

our $COMMENT                    = '';
our @PREFIX                     = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install' );
our @libdir                     = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\lib' );
our @BINDIR                     = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\bin' );
our @BINDIR_REL_PREFIX          = ( 'bin' );
our @LIBDIR                     = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\lib' );
our @LIBDIR_REL_PREFIX          = ( 'lib' );
our @INCLUDEDIR                 = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\include' );
our @INCLUDEDIR_REL_PREFIX      = ( 'include' );
our @APPLINKDIR                 = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\include\openssl' );
our @APPLINKDIR_REL_PREFIX      = ( 'include/openssl' );
our @ENGINESDIR                 = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\lib\engines-3' );
our @ENGINESDIR_REL_LIBDIR      = ( 'engines-3' );
our @MODULESDIR                 = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\lib\ossl-modules' );
our @MODULESDIR_REL_LIBDIR      = ( 'ossl-modules' );
our @PKGCONFIGDIR               = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\lib' );
our @PKGCONFIGDIR_REL_LIBDIR    = ( '' );
our @CMAKECONFIGDIR             = ( 'C:\Users\gutii\source\repos\TeleCrap\out\build\x64-Debug\third-party\openssl\install\lib\cmake\OpenSSL' );
our @CMAKECONFIGDIR_REL_LIBDIR  = ( 'cmake\OpenSSL' );
our $VERSION                    = '3.6.2';
our @LDLIBS                     =
    # Unix and Windows use space separation, VMS uses comma separation
    $^O eq 'VMS'
    ? split(/ *, */, 'ws2_32.lib gdi32.lib advapi32.lib crypt32.lib user32.lib ')
    : split(/ +/, 'ws2_32.lib gdi32.lib advapi32.lib crypt32.lib user32.lib ');

1;
