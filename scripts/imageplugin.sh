#!/bin/bash
#
# $Id$
#
# script for vdr-imageplugin to convert the selected image to pnm-image
# needs : netpbm-progs > anytopnm pnmscalefixed pnmfile pnmcut pnmflip
#
# History:
# 2005-07-26 add commando for rotate 180
# 2005-07-18 wrong lookup for pnmscale and really are pnmscalefixed used
# 2004-08-12 Inital release
#    base on prior work for convert.sh 
#      by  Onno Kreuzinger <o.kreuzinger-at-kreuzinger.biz> 
#          Andreas Holzhammer and <Interpohl-at-vdr-portal.de>
#
################################################################################
# Userconfig:
################################################################################
# if your install external software like netpbm outside /bin:/usr/bin, adjust folder
PATH=/usr/local/bin:$PATH
# Set to "yes" to speedup reviewed image 
TMPCACHE=no
# Set to "no" if this script work and your don't need success messages  
VERBOSE=yes
# Set to "yes" if this script don't work, only usable if your self execute the script from shell
DEBUG=no

################################################################################
# and now the script
################################################################################
[ "z$DEBUG" = "zyes" ] && set -xv
SCRIPTNAME=$(basename "$0")
{
[ "z$VERBOSE" = "zyes" ] && echo "called '$SCRIPTNAME $*'" 

if [ $# -lt 7 ] ; then
  echo "Usage: $SCRIPTNAME infile outfile WIDTH HEIGHT ZOOMFACTOR LEFTPOS TOPPOS [FLIPCMD]" 1>&2
  echo " e.g.: $SCRIPTNAME in.png out.pnm 720 576 0 0 0" 1>&2
  echo " or .: $SCRIPTNAME in.png out.pnm 720 576 3 360 360 left" 1>&2
  echo "" 1>&2
  echo "WIDTH      - Width of TVScreen             (720)" 1>&2
  echo "HEIGHT     - Height of TVScreen            (480..576)" 1>&2
  echo "ZOOMFACTOR - Zoomfactor                    (0....10)" 1>&2
  echo "LEFTPOS    - Offset from left on Zoommode  (0......)" 1>&2
  echo "TOPPOS     - Offset from top on Zoommode   (0......)" 1>&2
  echo "FLIPCMD    - optional should image flip    (left,right,rotated,original)" 1>&2
  exit 1
fi 

  # Defaultvalue, overwrite with env from plugin
  ASPECT_RATIO="${ASPECT_RATIO:-"4:3"}"
  
  # check requirement external programs
  REQUIREMENTS="anytopnm pnmscalefixed pnmfile pnmcut pnmflip"
  for i in $REQUIREMENTS
  do 
    type "$i" > /dev/null 2>&1
    [ $? -ne 0 ] && echo -e "$SCRIPTNAME: Error ! External required program: \"$i\" not found !\n Please adjust PATH or install it inside follow Folder \"$PATH\" \n" && exit 1
  done
  
  INFILE="$1"
  OUTFILE="$2"
  OUT_DISPLAY_X=$3
  OUT_DISPLAY_Y=$4
  ZOOMFACTOR=$5
  LEFTPOS=$6
  TOPPOS=$7
  FLIPCMD="$8"
  
  OUTDIR=$(dirname "$OUTFILE")
  [ ! -d "$OUTDIR" ] && mkdir -p "$OUTDIR"
  
  TMPFILE="$OUTFILE.tmp"
  PARFILE="$OUTFILE.par"
  
  # remove precreated files if called with flip "left","right" or "original"
  [ -s "$OUTFILE" ] && [ "$FLIPCMD" != "" ] &&  rm -f "$OUTFILE"
  [ -s "$TMPFILE" ] && [ "$FLIPCMD" != "" ] &&  rm -f "$TMPFILE"

  if [ -s "$OUTFILE" ] ; then
    [ "z$VERBOSE" = "zyes" ] && echo "Success! Convert not required, $OUTFILE exists already!" 
    exit 0
  else

    # Convert Image to PNM File
    [  ! -s "$TMPFILE" ]  && anytopnm "$INFILE" > "$TMPFILE"  
    # if success    
    if [ -s "$TMPFILE" ] ; then
    
      # Get image resolution
      RES=$(pnmfile < "$TMPFILE" -) # checked with netpbm 10.0,
      # Parse pnmfile output "-: PPM raw, 768 by 576  maxval 255" => 768 x 576
      X_RES=$(echo -e "$RES"| cut -d " " -f 4)
      Y_RES=$(echo -e "$RES"| cut -d " " -f 6)
      
      # set flip command
      case "$FLIPCMD" in
        right )
        FLIP="pnmflip -rotate270"
        SWAPRES=$X_RES;X_RES=$Y_RES;Y_RES=$SWAPRES
        ;;
        left )
        FLIP="pnmflip -rotate90";
        SWAPRES=$X_RES;X_RES=$Y_RES;Y_RES=$SWAPRES
        ;; 
        rotated )
        FLIP="pnmflip -rotate180";
        ;; 
        *)
        FLIP="cat";
        ;;
      esac
      # Save config for plugin as readable file 
      echo "$X_RES" "$Y_RES" "$FLIPCMD" > "$PARFILE"
      
      # define aspect ratio depends plugin setup
      if [ $ASPECT_RATIO = "16:9" ] ; then 
        SCALE_MIN_ASP=163
        SCALE_MAX_ASP=178
      else 
        SCALE_MIN_ASP=125
        SCALE_MAX_ASP=133
      fi 
  
      # if zoom image, zoom it with factor
      if [ "$ZOOMFACTOR" -gt 0 ] ; then
      
        ZOOM_X=$(($X_RES*$ZOOMFACTOR))
        ZOOM_Y=$(($Y_RES*$ZOOMFACTOR))
        
        $FLIP < "$TMPFILE" | pnmscalefixed -xysize $ZOOM_X $ZOOM_Y | \
          pnmcut -pad -left $LEFTPOS -top $TOPPOS -width $OUT_DISPLAY_X -height $OUT_DISPLAY_Y \
          > "$OUTFILE"
          
      # else scale image to TV Screensize
      else

        if [ "$((${X_RES}00 / ${Y_RES}))" -lt $SCALE_MIN_ASP ] ; then
          OUT_DISPLAY_X=$((${OUT_DISPLAY_Y}000 / $Y_RES * $X_RES / 1000))
        elif [ "$((${X_RES}00 / ${Y_RES}))" -gt $SCALE_MAX_ASP ] ; then
          OUT_DISPLAY_Y=$((${OUT_DISPLAY_X}000 / $X_RES * $Y_RES / 1000))
        fi
      
         $FLIP < "$TMPFILE" | \
          pnmscalefixed -xysize $OUT_DISPLAY_X $OUT_DISPLAY_Y \
          > "$OUTFILE"

      fi
      # if'nt tmpfile use for next view, remove tmp file
      [ "z$TMPCACHE" = "zno" ] && rm -f "$TMPFILE"
    fi
  fi

  if [ -s "$OUTFILE" ] ; then 
    [ "z$VERBOSE" = "zyes" ] && echo "Success! Stopped with created $OUTFILE"
    exit 0 # Creation seem success, tell it with 'exit 0' to plugin
  fi
  [ "z$VERBOSE" = "zyes" ] && echo "Error! Stopped without found created $OUTFILE, converting should failed !" 
  exit 1 # Hmm, created is failed tell it with 'exit 1' to plugin


###### >>>>>>> !!! Only one of the follow lines are allowed (begins always with 2>&1 ) !!!
### Dump any message to syslog to see use cat /var/log/messages | grep imageplugin
}  2>&1 | logger -s -t "$SCRIPTNAME"
### If your wish don't any message logging
# 2>&1 > /dev/null
### If your wish old style message logging
# 2>&1 > /tmp/image/convert.log
