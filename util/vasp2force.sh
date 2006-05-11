#!/bin/sh
#/****************************************************************
#* $Revision: 1.4 $
#* $Date: 2006/05/11 07:27:39 $
#*****************************************************************/

[ -f ../single_atom_energies ] || { echo file ../single_atom_energies not found; exit;}
wdir=`pwd`
cat OUTCAR | awk -v wdir="${wdir}" '  BEGIN { 
    getline saeng < "../single_atom_energies"; 
    getline < "POSCAR"; getline scale < "POSCAR";
    getline boxx < "POSCAR"; getline boxy < "POSCAR"; getline boxz < "POSCAR";
    getline < "POSCAR"; ntypes = split($0,a);single_energy=0.;
    split(saeng,sae);
    #sae[1]=-0.000219; sae[2]=-0.993872; sae[3]=-0.855835;
    for (i=1; i<=ntypes; i++) single_energy += a[i]*sae[i];
     for (i=2; i<=ntypes; i++) a[i]=a[i-1]+$i;
    j=1; for (i=1; i<=a[ntypes]; i++) { if (i>a[j]) j++; b[i]=j-1; }
    split(boxx,boxx_v);
    split(boxy,boxy_v);
    split(boxz,boxz_v);
  };
# Marker for energy is two spaces between energy and without.
# Correct energy is energy(sigma->0)
  /energy  without/ {
    energy=($7-single_energy)/a[ntypes];
    delete stress;
  }
# Find box vectors
  /VOLUME and BASIS/ {
    for (i=1;i<=4;i++) getline;
    getline boxx; getline boxy; getline boxz;
    split(boxx,boxx_v);
    split(boxy,boxy_v);
    split(boxz,boxz_v);
}
  ($2=="kB") { 
     for (i=1;i<=6;i++) stress[i]=$(i+2)/1602.;
}
  ($2=="TOTAL-FORCE") { 
     print "#N",a[ntypes],1; #flag indicates whether to use forces or not
     print "## force file generated from directory " wdir;
     print "#X",boxx_v[1]*scale " " boxx_v[2]*scale " " boxx_v[3]*scale; 
     print "#Y",boxy_v[1]*scale " " boxy_v[2]*scale " " boxy_v[3]*scale; 
     print "#Z",boxz_v[1]*scale " " boxz_v[2]*scale " " boxz_v[3]*scale; 
     printf("#E %.10f\n",energy) ;
     if ( 1 in stress ) 
       print "#S",stress[1],stress[2],stress[3],stress[4],stress[5],stress[6];
     print "#F";  
     getline; getline;
     for (i=1; i<=a[ntypes]; i++) { print b[i],$1,$2,$3,$4,$5,$6; getline; } 
  };' 
