tclfcgi
=======

Tcl binding to FastCGI - Linux

This creates a Tcl package (Fcgi) that can be used by Tcl based FastCGI scripts.  This package is tested on Debian Wheezy (3.2.0-4-686-pae) with Apache Apache/2.2.22 mod_fcgi and Tcl8.5.14

Pre-requisite:

    sudo apt-get install libfcgi-dev libfcgi0ldbl

Build & Install:

    cd c-src
    make
    sudo make install

Apache Setup:

###1. Enable fastcgi module in apache2 using:

     sudo a2enmod fastcgi

###2. Place following lines in /etc/apache2/conf.d/httpd.conf

     ScriptAlias /appname/ /my/path/to/fcgi-bin/
     
     <Directory "/my/path/to/fcgi-bin/">
         SetHandler fastcgi-script
         AllowOverride None
         Options +ExecCGI -MultiViews +SymLinksIfOwnerMatch
         Order allow,deny
         Allow from all
     </Directory>

###3. Place your FCGI scripts in /my/path/to/fcgi-bin and set executable permission to the scripts. Example Tcl FCGI script (example.fcg):

    do_one_time_app_initialization_here
    while {[FCGI_Accept] >= 0 } {
        
        ::ncgi::parse
        set var1 [::ncgi::value var1]
        set var2 [::ncgi::value var2]
        
        set result [do_processing $var1 $var2] 
            ::ncgi::header
            puts $result
            ::ncgi::reset
      }
      

###4. Access your FCGI script from browser using:

    http://your.com/appname/example.fcg

References:

    http://www.fastcgi.com/devkit/doc/fcgi-tcl.htm
    http://sourceforge.net/projects/tcl-fastcgi/

