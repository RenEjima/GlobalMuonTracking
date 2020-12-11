
I've prepared an easy to use version of the Hiroshima matching function that I talked about at the WP9 meeting the other day.

The changes I made are in the following files.

MUONMatcher.cxx
MUONMatcher.h
runMatching.C

How to use

    If you want to try Hiroshima's matching function, please enter the following command

       ./matcher.sh --match --matchFcn Hiroshima -o OUTPUTDIR

    if you want to specify a MatchingPlaneZ, type the following command.

       ./matcher.sh --match --matchFcn Hiroshima --matchPlaneZ -90.0 -o OUTPUTDIR



In Hiroshima's matching function, I think that matchingplaneZ is best at -90.
The main calculation of the matching is written at the bottom of MUONMatcher.cxx.
It may be full of bugs, so please let me know if you find a bug.
The vague idea is described in the slides I presented at a WP9 meeting the other day. 
If you have any questions about the concept, please email me.




Ren Ejima (ejima@quark.hiroshima-u.ac.jp)
Hiroshima University (Undergraduate student)
