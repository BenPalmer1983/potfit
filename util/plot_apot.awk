#!/usr/bin/awk -f
#####################################################################
#
# plot_pot.awk: create gnuplot readable potential from
#		from analytic potential file format
#
####################################################################
#
#   Copyright 2008-2009 Daniel Schopf
#             Institute for Theoretical and Applied Physics
#             University of Stuttgart, D-70550 Stuttgart, Germany
#             http://www.itap.physik.uni-stuttgart.de/
#
####################################################################
#
#   This file is part of potfit.
#
#   potfit is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   potfit is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with potfit; if not, write to the Free Software
#   Foundation, Inc., 51 Franklin St, Fifth Floor,
#   Boston, MA  02110-1301  USA
#
####################################################################
# $Revision: 1.9 $
# $Date: 2009/03/06 08:52:45 $
####################################################################
#
# Usage: plot_pot.awk pot_file_1 pot_file_2 ... pair_file
#
# The resulting potential is written to standard output.
#
####################################################################

BEGIN {
	count=0;
	mindist=10;
	maxdist=0;
	ORS="";
}

{
	for (a=0;a<(ARGC-1);a++) {
		if (substr($0,3,6)=="radial") {
			dist_file = ARGV[a+1];
		}
		else {
			while (substr($0,2,1)!="F") getline;
		if ($2 != 0) {
			print "Error - wrong potential format of " ARGV[ARGIND] "\n" ;
			exit 2;
		}
		total_pots=$3;
		if (int(total_pots)!=total_pots) {
			print "ERROR - incorrect parameter file " ARGV[ARGIND]  "\n" ;
			exit 2;
		}
		for (i=count;i<(count+total_pots);i++){
			while (substr($0,1,4)!="type") getline;
			pot_name[i] = $2;
			if (match(pot_name[i],"_sc$",arr)>0)
				pot_name[i] = substr(pot_name[i],1,length(pot_name[i])-3);
			if (pot_name[i]=="eopp")
				n_param[i]=6;
			else if (pot_name[i]=="lj")
				n_param[i]=2;
			else if (pot_name[i]=="morse")
				n_param[i]=3;
			else if (pot_name[i]=="softshell")
				n_param[i]=2;
			else if (pot_name[i]=="eoppexp")
				n_param[i]=6;
			else if (pot_name[i]=="meopp")
				n_param[i]=7;
			getline;
			if ($2>maxdist)
				maxdist=$2;
			getline;
			if (substr($0,1,1)=="#") {
				if ($3<mindist)
					mindist=$3;
				if ($5>maxdist)
					maxdist=$5;
				getline;
			}
			for (l=1;l<=n_param[i];l++) {
				params[i "," l ] = $2;
				getline
			}
		}
		count = count + total_pots;
	}
}
nextfile;
}

END {
	if (count != 0) {
		print "reset;\n" > "plot";
		print "set grid;\n" > "plot";
		print "set arrow 1 from " maxdist ",.2 to " maxdist ",-.2 nohead size 2,15,10 lw 2;\n" > "plot";
		print "set label \"cutoff\" at " maxdist*0.95 ",0.23;\n" > "plot";
		print "pl [" 0.5*mindist ":" maxdist*1.1 "][-0.3:.6] " > "plot";
		for (i=0;i<count;i++) {
			if (pot_name[i] == "eopp") {
				printf "%f/x**%f+%f/x**%f*cos(%f*x+%f) w l",params[i","1],params[i","2],params[i","3],params[i","4],params[i","5],params[i","6] > "plot";
			} else if (pot_name[i] == "lj") {
				printf "4*%f*((%f/x)**12-(%f/x)**6) w l",params[i","1],params[i","2],params[i","2] > "plot";
			} else if (pot_name[i] == "morse") {
				printf "%f*(exp(-2*%f*(x-%f))-2*exp(-%f*(x-%f))) w l",params[i","1],params[i","2],params[i","3],params[i","2],params[i","3] > "plot";
			} else if (pot_name[i] == "softshell") {
				printf "(%f/x)**%f w l",params[i","1],params[i","2] > "plot";
			} else if (pot_name[i] == "eoppexp") {
				printf "%f*exp(-%f*x)+%f/x**%f*cos(%f*x+%f) w l",params[i","1],params[i","2],params[i","3],params[i","4],params[i","5],params[i","6] > "plot";
			} else if (pot_name[i] == "meopp") {
				printf "%f/(x-%f)**%f+%f/x**%f*cos(%f*x+%f) w l",params[i","1],params[i","7],params[i","2],params[i","3],params[i","4],params[i","5],params[i","6] > "plot";
			}
		if (i!=(count-1))
			print ", " > "plot";
		}
		if (dist_file != "") {
			print ", " > "plot";
			for (i=0;i<count;i++) {
				if (i==0)
					print "'" dist_file "' i " i " w lines t \"rad_dist pot " i "\"" > "plot";
				else
					print "'' i " i " w lines t \"rad_dist pot " i "\"" > "plot";
				if (i!=(count-1))
					print ", " > "plot";
			}
		}
		print ";" > "plot";
		system("gnuplot -persist plot");
	}
}
