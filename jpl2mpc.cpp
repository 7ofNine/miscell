/* Copyright (C) 2018, Project Pluto

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

/* Code to convert an ephemeris gathered from JPL's _Horizons_ system into
the format used by MPC's DASO service,  or for generating TLEs with
my 'eph2tle' program.  Based largely on code I'd already written to
import _Horizons_ data to the .b32 format used in Guide.

   It can convert either position-only ephemerides (which is what are
used for MPC's DASO service) or state vector ephems (which is the
input needed for my 'eph2tle' software to fit TLEs.)  The output
is in equatorial J2000,  AU,  and AU/day (if the input is in ecliptic
coordinates and/or km and km/s,  the vectors are rotated and scaled
accordingly;  MPC and eph2tle are both particular about taking only
equatorial AU/day data.)

   The following looks up a couple of JPL identifiers to get human-readable
names.  If a name is set properly,  it can be used automatically when
'eph2tle' tries to fit Two-Line Elements,  and that program will spot the
international and NORAD designations.  At present,  only TESS,  Chandra,
and Gaia are recognized,  but others can obviously be added.   */

static const char *look_up_name( const int idx)
{
   if( idx == -21)
      return( "SOHO");
   if( idx == -48)
      return( "Hubble Space Telescope");
   if( idx == -82)
      return( "Cassini");
   if( idx == -234)
      return( "STEREO-A");
   if( idx == -235)
      return( "STEREO-B");
   if( idx == -144)
      return( "Solar Orbiter");
   if( idx == -95)
      return( "TESS = 2018-038A = NORAD 43435");
   if( idx == -79)
      return( "Spitzer Space Telescope");
   if( idx == -96)
      return( "Parker Space Probe");
   if( idx == -98)
      return( "New Horizons");
   if( idx == -151)
      return( "Chandra = 1999-040B = NORAD 25867");
   if( idx == -163)
      return( "WISE");
   if( idx == -139479)
      return( "Gaia = 2013-074A = NORAD 39479");
   if( idx == -9901491)
      return( "Tianwen-1 = 2020-049A = NORAD 45935");
   if( idx == -37)
      return( "Hayabusa 2 = 2014-076A = NORAD 40319");
   return( "");
}

static int get_coords_from_buff( double *coords, const char *buff, const bool is_ecliptical)
{
   int xloc = 1, yloc = 24, zloc = 47;

   if( buff[1] == 'X' || buff[2] == 'X')     /* quantities are labelled */
      {
      xloc = 4;
      yloc = 30;
      zloc = 56;
      }
   coords[0] = atof( buff + xloc);
   coords[1] = atof( buff + yloc);
   coords[2] = atof( buff + zloc);
   if( is_ecliptical)         /* rotate to equatorial */
      {
      static const double sin_obliq_2000 = 0.397777155931913701597179975942380896684;
      static const double cos_obliq_2000 = 0.917482062069181825744000384639406458043;
      const double temp    = coords[2] * cos_obliq_2000 + coords[1] * sin_obliq_2000;

      coords[1] = coords[1] * cos_obliq_2000 - coords[2] * sin_obliq_2000;
      coords[2] = temp;
      }
   return( 0);
}

int main( const int argc, const char **argv)
{
   FILE *ifile = (argc > 1 ? fopen( argv[1], "rb") : NULL);
   FILE *ofile = (argc > 2 ? fopen( argv[2], "wb") : stdout);
   char buff[200];
   unsigned n_written = 0;
   double jd0 = 0., step_size = 0., jd, frac_jd0 = 0.;
   int int_jd0 = 0;
   bool state_vectors = false, is_equatorial = false;
   bool is_ecliptical = false, in_km_s = false;
   const char *header_fmt = "%13.5f %14.10f %4u";
   const char *object_name = "";
   const double AU_IN_KM = 1.495978707e+8;
   const double seconds_per_day = 24. * 60. * 60.;

   if( argc > 1 && !ifile)
      printf( "\nCouldn't open the Horizons file '%s'\n", argv[1]);
   if( !ofile)
      printf( "\nCouldn't open the output file '%s'\n", argv[2]);

   if( argc < 2 || !ifile || !ofile)
      {
      printf( "\nJPL2MPC takes input ephemeri(de)s generated by HORIZONS  and,\n");
      printf( "produces file(s) suitable for use in DASO or eph2tle.  The name of\n");
      printf( "the input ephemeris must be provided as a command-line argument.\n");
      printf( "For example:\n");
      printf( "\njpl2mpc gaia.txt\n\n");
      printf( "The JPL ephemeris must be in text form (can use the 'download/save'\n");
      printf( "option for this).\n");
      printf( "   The bottom of 'jpl2mpc.cpp' shows how to submit a job via e-mail\n");
      printf( "to the Horizons server that will get you an ephemeris in the necessary\n");
      printf( "format,  or how to get such ephemerides using a URL.\n");
      exit( -1);
      }

   fseek( ifile, 0L, SEEK_SET);
   fprintf( ofile, header_fmt, 0., 0., 0);
   fprintf( ofile, " 0,1,1");  /* i.e.,  ecliptic J2000 in AU and days */
   while( fgets( buff, sizeof( buff), ifile))
      if( (jd = atof( buff)) > 2000000. && jd < 3000000. &&
               strlen( buff) > 54 && !memcmp( buff + 17, " = A.D.", 7)
               && buff[42] == ':' && buff[45] == '.'
               && !memcmp( buff + 50, " TDB", 4))
         {
         double coords[3];
         int i;

         if( is_equatorial == is_ecliptical)       /* should be one or the other */
            {
            printf( "Input coordinates must be in the Earth mean equator and equinox\n");
            printf( "or in J2000 ecliptic coordinates\n");
            return( -1);
            }
         if( !n_written)
            {
            jd0 = jd;
            int_jd0 = atoi( buff);
            frac_jd0 = atof( buff + 7);
            if( *object_name)
               fprintf( ofile, " (500) Geocentric: %s\n", object_name);
            else
               fprintf( ofile, "\n");
            }
         else if( n_written == 1)
            step_size = (atof( buff + 7) - frac_jd0)
                                  + (double)( atoi( buff) - int_jd0);
         if( !fgets( buff, sizeof( buff), ifile))
            {
            printf( "Failed to get data from input file\n");
            return( -2);
            }
         get_coords_from_buff( coords, buff, is_ecliptical);
         if( in_km_s)
            for( i = 0; i < 3; i++)
               coords[i] /= AU_IN_KM;
         fprintf( ofile, "%13.5f%16.10f%16.10f%16.10f", jd,
                  coords[0], coords[1], coords[2]);
         if( !state_vectors)
            fprintf( ofile, "\n");
         else
            {
            if( !fgets( buff, sizeof( buff), ifile))
               {
               printf( "Failed to get data from input file\n");
               return( -2);
               }
            get_coords_from_buff( coords, buff, is_ecliptical);
            if( in_km_s)
               for( i = 0; i < 3; i++)
                  coords[i] *= seconds_per_day / AU_IN_KM;
            fprintf( ofile, " %16.12f%16.12f%16.12f\n",
                  coords[0], coords[1], coords[2]);
            }
         n_written++;
         }
      else if( !memcmp( buff, "   VX    VY    VZ", 17))
         state_vectors = true;
      else if( strstr( buff, "Earth Mean Equator and Equinox"))
         is_equatorial = true;
      else if( strstr( buff, "Reference frame : ICRF"))
         is_equatorial = true;
      else if( strstr( buff, "Ecliptic and Mean Equinox of Reference Epoch"))
         is_ecliptical = true;
      else if( strstr( buff, "Reference frame : Ecliptic of J2000"))
         is_ecliptical = true;
      else if( !memcmp( buff, " Revised:", 9))
         object_name = look_up_name( atoi( buff + 71));
      else if( !memcmp( buff, "Target body name:", 17))
         {
         const char *tptr = strstr( buff, "(-");

         if( tptr)
            object_name = look_up_name( atoi( tptr + 1));
         }
      else if( !memcmp( buff,  "Output units    : KM-S", 22))
         in_km_s = true;

   fprintf( ofile, "\n\nCreated from Horizons data by 'jpl2mpc', ver %s\n",
                                        __DATE__);
                     /* Seek back to start of input file & write header data: */
   fseek( ifile, 0L, SEEK_SET);
   while( fgets( buff, sizeof( buff), ifile) && memcmp( buff, "$$SOE", 5))
      fprintf( ofile, "%s", buff);

                     /* Seek back to start of file & write corrected hdr: */
   fseek( ofile, 0L, SEEK_SET);
   fprintf( ofile, header_fmt, jd0, step_size, n_written);

   fclose( ifile);
// fprintf( ofile, "Ephemeris from JPL Horizons output\n");
// fprintf( ofile, "Created using 'jpl2mpc', version %s\n", __DATE__);
// fprintf( ofile, "Ephemeris converted %s", ctime( &t0));
// printf( "JD0: %f   Step size: %f   %ld steps\n",
//                             jd0,  step_size, (long)n_written);
   return( 0);
}

/* Following is an example e-mail request to the Horizons server for a
suitable text ephemeris for Gaia (followed by a similar example
showing how to send a request on a URL,  which is probably the
method I'll be using in the future... you get the same result
either way,  but the URL modification is a little easier.)

   For other objects,  you would modify the COMMAND and possibly
CENTER lines in the following (if you didn't want geocentric vectors)
as well as the START_TIME,  STOP_TIME, and STEP_SIZE. And, of course,
the EMAIL_ADDR.

   Aside from that,  all is as it should be:  vectors are requested
with positions (or positions/velocities),  with no light-time corrections.

   After making those modifications,  you would send the result to
horizons@ssd.jpl.nasa.gov, subject line JOB.

!$$SOF (ssd)       JPL/Horizons Execution Control VARLIST
! Full directions are at
! ftp://ssd.jpl.nasa.gov/pub/ssd/horizons_batch_example.long

! EMAIL_ADDR sets e-mail address output is sent to. Enclose
! in quotes. Null assignment uses mailer return address.

 EMAIL_ADDR = 'pluto@projectpluto.com'
 COMMAND    = 'Gaia'

! MAKE_EPHEM toggles generation of ephemeris, if possible.
! Values: YES or NO

 MAKE_EPHEM = 'YES'

! TABLE_TYPE selects type of table to generate, if possible.
! Values: OBSERVER, ELEMENTS, VECTORS
! (or unique abbreviation of those values).

 TABLE_TYPE = 'VECTORS'
 CENTER     = '500@399'
 REF_PLANE  = 'FRAME'

! START_TIME specifies ephemeris start time
! (i.e. YYYY-MMM-DD {HH:MM} {UT/TT}) ... where braces "{}"
! denote optional inputs. See program user's guide for
! lists of the numerous ways to specify times. Time zone
! offsets can be set. For example, '1998-JAN-1 10:00 UT-8'
! would produce a table in Pacific Standard Time. 'UT+00:00'
! is the same as 'UT'. Offsets are not applied to TT
! (Terrestrial Time) tables. See TIME_ZONE variable also.

 START_TIME = '2014-OCT-14 00:00 TDB'

! STOP_TIME specifies ephemeris stop time
! (i.e. YYYY-MMM-DD {HH:MM}).

 STOP_TIME  = '2016-JAN-01'
 STEP_SIZE  = '1 day'
 QUANTITIES = '
 REF_SYSTEM = 'J2000'
 OUT_UNITS  = 'AU-D'

! VECT_TABLE = 1 means XYZ only,  no velocity, light-time,
! range, or range-rate.  Use VECT_TABLE = 2 to also get the
! velocity,  to produce state vector ephemerides resembling
! those from Find_Orb :
 VECT_TABLE = '1'

! VECT_CORR selects level of correction: NONE=geometric states
! (which we happen to want); 'LT' = astrometric states, 'LT+S'
! = same with stellar aberration included.
 VECT_CORR = 'NONE'

 CAL_FORMAT = 'CAL'

!$$EOF++++++++++++++++++++++++++++++++++++++++++++++++++++++

https://ssd.jpl.nasa.gov/horizons_batch.cgi?batch=1&COMMAND='-139479'&OBJ_DATA='NO'&TABLE_TYPE='V'&START_TIME='2020-01-01'&STOP_TIME='2021-01-01'&STEP_SIZE='3660'&VEC_TABLE='2'&VEC_LABELS='N'

For TESS,  2021 :

https://ssd.jpl.nasa.gov/horizons_batch.cgi?batch=1&COMMAND='-95'&OBJ_DATA='NO'&TABLE_TYPE='V'&START_TIME='2021-01-01'&STOP_TIME='2022-01-01'&STEP_SIZE='3650'&VEC_TABLE='2'&VEC_LABELS='N'

*/
