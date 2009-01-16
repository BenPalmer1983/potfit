/****************************************************************
* 
*  config.c: Reads atomic configurations and forces.
* 
*****************************************************************/
/*
*   Copyright 2002-2009 Peter Brommer, Franz G"ahler, Daniel Schopf
*             Institute for Theoretical and Applied Physics
*             University of Stuttgart, D-70550 Stuttgart, Germany
*             http://www.itap.physik.uni-stuttgart.de/
*
*****************************************************************/
/*  
*   This file is part of potfit.
*
*   potfit is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   potfit is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with potfit; if not, write to the Free Software
*   Foundation, Inc., 51 Franklin St, Fifth Floor, 
*   Boston, MA  02110-1301  USA
*/
/****************************************************************
* $Revision: 1.41 $
* $Date: 2009/01/16 08:36:22 $
*****************************************************************/

#include "potfit.h"

/*****************************************************************************
*
*  read the configurations
*
******************************************************************************/

void read_config_old(char *filename)
{
  FILE *infile;
  char  msg[255];
  int   count, i, j, k, maxneigh = 0;
  atom_t *atom;
  neigh_t *neigh;

  nconf = 0;

  /* open file */
  infile = fopen(filename, "r");
  if (NULL == infile) {
    sprintf(msg, "Could not open file %s\n", filename);
    error(msg);
  }

  /* read configurations until the end of the file */
  do {

    /* number of atoms in this configuration */
    if (1 > fscanf(infile, "%d", &count))
      error("Unexpected end of file");

    /* increase memory for this many additional atoms */
    atoms = (atom_t *)realloc(atoms, (natoms + count) * sizeof(atom_t));
    if (NULL == atoms)
      error("Cannot allocate memory for atoms");
    force_0 = (real *)realloc(force_0, 3 * (natoms + count) * sizeof(real));
    if (NULL == force_0)
      error("Cannot allocate memory for forces");

    /* read the atoms */
    for (i = 0; i < count; i++) {

      k = 3 * (natoms + i);
      atom = atoms + natoms + i;
      if (8 > fscanf(infile, "%d %lf %lf %lf %lf %lf %lf %d\n", &(atom->typ),
		     &(atom->pos.x), &(atom->pos.y), &(atom->pos.z),
		     force_0 + k, force_0 + k + 1, force_0 + k + 2,
		     &(atom->n_neigh)))
	error("Corrupt configuration file");

      /* check if n_neigh < MAXNEIGH */
      maxneigh = MAX(maxneigh, atom->n_neigh);
      if (maxneigh >= MAXNEIGH) {
	sprintf(msg, "%d neighbors, MAXNEIGH is %d!", maxneigh, MAXNEIGH);
	error(msg);
      }

      /* read the neighbors */
      for (j = 0; j < atom->n_neigh; j++) {
	neigh = atom->neigh + j;
	if (5 > fscanf(infile, "%d %lf %lf %lf %lf\n",
		       &(neigh->typ), &(neigh->r),
		       &(neigh->dist.x), &(neigh->dist.y), &(neigh->dist.z)))
	  error("Corrupt configuration file");
      }
    }
    mdim = k + 3;		/* mdim is dimension of force vector */

    /* increment natoms and configuration number */
    natoms += count;
    nconf++;

  } while (!feof(infile));

  /* print diagnostic message and close file */
  printf("Maximal number of neighbors is %d, MAXNEIGH is %d\n",
	 maxneigh, MAXNEIGH);
  printf("Read %d configurations with a total of %d atoms\n", nconf, natoms);
  fclose(infile);
}



/* vector product */
vektor vec_prod(vektor u, vektor v)
{
  vektor w;
  w.x = u.y * v.z - u.z * v.y;
  w.y = u.z * v.x - u.x * v.z;
  w.z = u.x * v.y - u.y * v.x;
  return w;
}

/******************************************************************************
*
*  compute box transformation matrix
*
******************************************************************************/

real make_box(void)
{
  real  volume;

  /* compute tbox_j such that SPROD(box_i,tbox_j) == delta_ij */
  /* first unnormalized */
  tbox_x = vec_prod(box_y, box_z);
  tbox_y = vec_prod(box_z, box_x);
  tbox_z = vec_prod(box_x, box_y);

  /* volume */
  volume = SPROD(box_x, tbox_x);
  if (0 == volume)
    error("Box edges are parallel");

  /* normalization */
  tbox_x.x /= volume;
  tbox_x.y /= volume;
  tbox_x.z /= volume;
  tbox_y.x /= volume;
  tbox_y.y /= volume;
  tbox_y.z /= volume;
  tbox_z.x /= volume;
  tbox_z.y /= volume;
  tbox_z.z /= volume;
  return volume;
}

/*****************************************************************************
*
*  read the configurations
*
******************************************************************************/

void read_config(char *filename)
{
  int   maxneigh = 0, count, index;
  int   i, j, k, ix, iy, iz, typ1, typ2, col, slot, klo, khi;
  int   h_stress, h_eng, h_boxx, h_boxy, h_boxz, use_force;
  int   w_force = 0, w_stress = 0;
  int   tag_format = 0;
  int   sh_dist = 0;		/* short distance flag */
  FILE *infile;
  char  msg[255], buffer[1024];
  char *res;
  atom_t *atom;
  neigh_t *neigh;
  stens *stresses;
  vektor d, dd;
  real  r, rr, istep, shift, step;

  real *mindist;
  mindist = (real *)malloc(ntypes * ntypes * sizeof(real));
  for (i = 0; i < ntypes * ntypes; i++)
    mindist[i] = rcut[i];

  nconf = 0;

  /* open file */
  infile = fopen(filename, "r");
  if (NULL == infile) {
    sprintf(msg, "Could not open file %s\n", filename);
    error(msg);
  }

  /* read configurations until the end of the file */
  do {
    res = fgets(buffer, 1024, infile);
    if (NULL == res)
      error("Unexpected end of file\n");
    if (res[0] == '#') {	/* new file type */
      tag_format = 1;
      h_eng = h_stress = h_boxx = h_boxy = h_boxz = 0;
      if (res[1] == 'N') {	/* Atom number line */
	if (sscanf(res + 3, "%d %d", &count, &use_force) < 2)
	  error("Error in atom number specification\n");
      } else
	error("Error - number of atoms missing\n");
    } else {
      /* number of atoms in this configuration */
      tag_format = 0;
      use_force = 1;
      if (1 > sscanf(buffer, "%d", &count))
	error("Unexpected end of file");
    }
    /* increase memory for this many additional atoms */
    atoms = (atom_t *)realloc(atoms, (natoms + count) * sizeof(atom_t));
    if (NULL == atoms)
      error("Cannot allocate memory for atoms");
    coheng = (real *)realloc(coheng, (nconf + 1) * sizeof(real));
    if (NULL == coheng)
      error("Cannot allocate memory for cohesive energy");
    volumen = (real *)realloc(volumen, (nconf + 1) * sizeof(real));
    if (NULL == volumen)
      error("Cannot allocate memory for volume");
    stress = (stens *)realloc(stress, (nconf + 1) * sizeof(stens));
    if (NULL == stress)
      error("Cannot allocate memory for stress");
    inconf = (int *)realloc(inconf, (nconf + 1) * sizeof(int));
    if (NULL == inconf)
      error("Cannot allocate memory for atoms in conf");
    cnfstart = (int *)realloc(cnfstart, (nconf + 1) * sizeof(int));
    if (NULL == cnfstart)
      error("Cannot allocate memory for start of conf");
    useforce = (int *)realloc(useforce, (nconf + 1) * sizeof(int));
    if (NULL == useforce)
      error("Cannot allocate memory for useforce");
    usestress = (int *)realloc(usestress, (nconf + 1) * sizeof(int));
    if (NULL == useforce)
      error("Cannot allocate memory for usestress");
    na_typ = (int **)realloc(na_typ, (nconf + 2) * sizeof(int *));
    if (NULL == na_typ)
      error("Cannot allocate memory for na_typ");
    na_typ[nconf] = (int *)malloc(ntypes * sizeof(int));
    if (NULL == na_typ[nconf])
      error("Cannot allocate memory for na_typ");

    for (i = 0; i < ntypes; i++)
      na_typ[nconf][i] = 0;

    inconf[nconf] = count;
    cnfstart[nconf] = natoms;
    useforce[nconf] = use_force;
    stresses = stress + nconf;

    if (tag_format) {
      do {
	res = fgets(buffer, 1024, infile);
	/* read the box vectors */
	if (res[1] == 'X') {
	  if (sscanf(res + 3, "%lf %lf %lf\n",
		     &box_x.x, &box_x.y, &box_x.z) == 3)
	    h_boxx++;
	  else
	    error("Error in box_x vector\n");
	} else if (res[1] == 'Y') {
	  if (sscanf(res + 3, "%lf %lf %lf\n",
		     &box_y.x, &box_y.y, &box_y.z) == 3)
	    h_boxy++;
	  else
	    error("Error in box_y vector\n");
	} else if (res[1] == 'Z') {
	  if (sscanf(res + 3, "%lf %lf %lf\n",
		     &box_z.x, &box_z.y, &box_z.z) == 3)
	    h_boxz++;
	  else
	    error("Error in box_z vector\n");
	} else if (res[1] == 'E') {
	  if (sscanf(res + 3, "%lf\n", &(coheng[nconf])) == 1)
	    h_eng++;
	  else
	    error("Error in energy\n");
	}

	/* read stress */
	else if (res[1] == 'S') {
	  if (sscanf(res + 3, "%lf %lf %lf %lf %lf %lf\n", &(stresses->xx),
		     &(stresses->yy), &(stresses->zz), &(stresses->xy),
		     &(stresses->yz), &(stresses->zx)) == 6)
	    h_stress++;
	  else
	    error("Error in stress tensor\n");
	}
      } while (res[1] != 'F');
      if (!(h_eng && h_boxx && h_boxy && h_boxz))
	error("Incomplete force file!");
      usestress[nconf] = h_stress;	/* no stress tensor available */
    } else {
      /* read the box vectors */
      fscanf(infile, "%lf %lf %lf\n", &box_x.x, &box_x.y, &box_x.z);
      fscanf(infile, "%lf %lf %lf\n", &box_y.x, &box_y.y, &box_y.z);
      fscanf(infile, "%lf %lf %lf\n", &box_z.x, &box_z.y, &box_z.z);

      /* read cohesive energy */
      if (1 != fscanf(infile, "%lf\n", &(coheng[nconf])))
	error("Configuration file without cohesive energy -- old format!");

      /* read stress tensor */
      if (6 != fscanf(infile, "%lf %lf %lf %lf %lf %lf\n", &(stresses->xx),
		      &(stresses->yy), &(stresses->zz), &(stresses->xy),
		      &(stresses->yz), &(stresses->zx)))
	error("No stresses given -- old format");
      usestress[nconf] = 1;
    }
    if (usestress[nconf])
      w_stress++;
    if (useforce[nconf])
      w_force++;
    volumen[nconf] = make_box();
    /* read the atoms */
    for (i = 0; i < count; i++) {
      k = 3 * (natoms + i);
      atom = atoms + natoms + i;
      if (7 > fscanf(infile, "%d %lf %lf %lf %lf %lf %lf\n", &(atom->typ),
		     &(atom->pos.x), &(atom->pos.y), &(atom->pos.z),
		     &(atom->force.x), &(atom->force.y), &(atom->force.z)))
	error("Corrupt configuration file");
      if (atom->typ >= ntypes || atom->typ < 0)
	error("Corrupt configuration file: Incorrect atom type");
      atom->absforce = sqrt(SQR(atom->force.x) +
			    SQR(atom->force.y) + SQR(atom->force.z));
      /* ++++++++++++++ */
//      printf("Atom %d, x %f, y %f, z %f, abs %f\n", natoms+i, atom->force.x, atom->force.y, atom->force.z, atom->absforce);
      atom->conf = nconf;
      na_typ[nconf][atom->typ] += 1;
    }

    /* compute the neighbor table */
    for (i = natoms; i < natoms + count; i++) {
      atoms[i].n_neigh = 0;
      for (j = natoms; j < natoms + count; j++) {
	d.x = atoms[j].pos.x - atoms[i].pos.x;
	d.y = atoms[j].pos.y - atoms[i].pos.y;
	d.z = atoms[j].pos.z - atoms[i].pos.z;
	for (ix = -1; ix <= 1; ix++)
	  for (iy = -1; iy <= 1; iy++)
	    for (iz = -1; iz <= 1; iz++) {
	      if ((i == j) && (ix == 0) && (iy == 0) && (iz == 0))
		continue;
	      dd.x = d.x + ix * box_x.x + iy * box_y.x + iz * box_z.x;
	      dd.y = d.y + ix * box_x.y + iy * box_y.y + iz * box_z.y;
	      dd.z = d.z + ix * box_x.z + iy * box_y.z + iz * box_z.z;
	      r = sqrt(SPROD(dd, dd));
	      typ1 = atoms[i].typ;
	      typ2 = atoms[j].typ;
#ifdef APOT
	      if (r <=
		  (rcut[typ1 * ntypes + typ2] *
		   (do_smooth ? CUTOFF_MARGIN : 1))) {
#else
	      if (r <= rcut[typ1 * ntypes + typ2]) {
#endif
		if (r <= rmin[typ1 * ntypes + typ2]) {
		  sh_dist = nconf;
		  fprintf(stderr, "Configuration %d: Distance %f\n", nconf,
			  r);
		  fprintf(stderr, "%d (type %d): %f %f %f\n", i - natoms,
			  typ1, atoms[i].pos.x, atoms[i].pos.y,
			  atoms[i].pos.z);
		  fprintf(stderr, "%d (type %d): %f %f %f\n", j - natoms,
			  typ2, dd.x, dd.y, dd.z);
		}
		if (atoms[i].n_neigh == MAXNEIGH)
		  error("Neighbor table is too small");
		dd.x /= r;
		dd.y /= r;
		dd.z /= r;
		k = atoms[i].n_neigh;
		atoms[i].neigh[k].typ = typ2;
		atoms[i].neigh[k].nr = j;
		atoms[i].neigh[k].r = r;
		atoms[i].neigh[k].dist = dd;
		atoms[i].n_neigh++;
		/* Minimal distance check */
/* 		if (mindist[ntypes*typ1+typ2]>r) */
/* 			printf("new mindist[%d]=%f i=%d k=%d\n",ntypes*typ1+typ2,r,i,k); */
		mindist[ntypes * typ1 + typ2] =
		  MIN(mindist[ntypes * typ1 + typ2], r);

		/* pre-compute index and shift into potential table */
		/* pair potential */
		if (!sh_dist) {
		  col = (typ1 <= typ2) ?
		    typ1 * ntypes + typ2 - ((typ1 * (typ1 + 1)) / 2)
		    : typ2 * ntypes + typ1 - ((typ2 * (typ2 + 1)) / 2);
		  if (format == 3 || format == 0) {
		    rr = r - calc_pot.begin[col];
		    if (rr < 0) {
		      printf("%f %f %d %d %d\n", r, calc_pot.begin[col], col,
			     nconf, i - natoms);
//                printf("%f %f %f %f %f %f\n", d.x,d.y,d.z,coheng[nconf],stresses->xx,stresses->yz);

		      fflush(stdout);
		      error("short distance in config.c!");
		    }
		    istep = calc_pot.invstep[col];
		    slot = (int)(rr * istep);
		    shift = (rr - slot * calc_pot.step[col]) * istep;
		    slot += calc_pot.first[col];
		    step = calc_pot.step[col];
#ifdef APOT
		    if (do_smooth) {
		      pot_list[col] =
			(int **)realloc(pot_list[col],
					(pot_list_length[col] +
					 1) * sizeof(int *));
		      pot_list[col][pot_list_length[col]] =
			(int *)malloc(2 * sizeof(int));
		      pot_list[col][pot_list_length[col]][0] = i;
		      pot_list[col][pot_list_length[col]][1] = k;
		      pot_list_length[col]++;
		    }
#endif

		  } else {	/* format == 4 ! */

		    klo = calc_pot.first[col];
		    khi = calc_pot.last[col];
		    /* bisection */
		    while (khi - klo > 1) {
		      slot = (khi + klo) >> 1;
		      if (calc_pot.xcoord[slot] > r)
			khi = slot;
		      else
			klo = slot;
		    }
		    slot = klo;
		    /* Check if we are at the last index - we should be lower */
		    /* should be impossible anyway */
		    /*  if (slot>=calc_pot.last[col]) {
		       klo--;khi--;
		       } */
		    step = calc_pot.xcoord[khi] - calc_pot.xcoord[klo];
		    shift = (r - calc_pot.xcoord[klo]) / step;

		  }
		  /* independent of format - we should be left of last index */
		  if (slot >= calc_pot.last[col]) {
		    slot--;
		    shift += 1.0;
		  }
		  atoms[i].neigh[k].shift[0] = shift;
		  atoms[i].neigh[k].slot[0] = slot;
		  atoms[i].neigh[k].step[0] = step;
#ifdef EAM
		  /* EAM part */
		  col = paircol + typ2;
		  if (format == 3) {
		    rr = r - calc_pot.begin[col];
		    if (rr < 0) {
		      printf("%f %f %d %d %d\n", r, calc_pot.begin[col], col,
			     typ1, typ2);
		      fflush(stdout);
		      error("short distance in config.c!");
		    }
		    istep = calc_pot.invstep[col];
		    slot = (int)(rr * istep);
		    shift = (rr - slot * calc_pot.step[col]) * istep;
		    slot += calc_pot.first[col];
		    step = calc_pot.step[col];
		  } else {	/* format == 4 ! */
		    klo = calc_pot.first[col];
		    khi = calc_pot.last[col];
		    /* bisection */
		    while (khi - klo > 1) {
		      slot = (khi + klo) >> 1;
		      if (calc_pot.xcoord[slot] > r)
			khi = slot;
		      else
			klo = slot;
		    }
		    slot = klo;
		    /* Check if we are at the last index - we should be lower */
		    /* should be impossible anyway */
		    /*   if (slot>=calc_pot.last[col]) {  */
		    /*    klo--;khi--; */
		    /*  } */
		    step = calc_pot.xcoord[khi] - calc_pot.xcoord[klo];
		    shift = (r - calc_pot.xcoord[klo]) / step;

		  }
		  /* Check if we are at the last index */
		  if (slot >= calc_pot.last[col]) {
		    slot--;
		    shift += 1.0;
		  }
		  atoms[i].neigh[k].shift[1] = shift;
		  atoms[i].neigh[k].slot[1] = slot;
		  atoms[i].neigh[k].step[1] = step;
#endif
		}
#ifdef APOT			/* just for emacs indentation */
	      }
#else
	      }
#endif

	    }
      }
      maxneigh = MAX(maxneigh, atoms[i].n_neigh);
    }

    /* increment natoms and configuration number */
    natoms += count;
    nconf++;

  } while (!feof(infile));
  fclose(infile);

  mdim = 3 * natoms + 7 * nconf;	/* mdim is dimension of force vector 
					   3*natoms are real forces, 
					   nconf cohesive energies,
					   6*nconf stress tensor components */
#ifdef EAM
  mdim += 2 * ntypes;		/* ntypes dummy constraints */
  mdim += nconf;		/* nconf limiting constraints */
#endif /* EAM */
  /* copy forces into single vector */
  if (NULL == (force_0 = (real *)malloc(mdim * sizeof(real))))
    error("Cannot allocate forces");
  k = 0;
  for (i = 0; i < natoms; i++) {	/* first forces */
    force_0[k++] = atoms[i].force.x;
    force_0[k++] = atoms[i].force.y;
    force_0[k++] = atoms[i].force.z;
  }
  for (i = 0; i < nconf; i++) {	/* then cohesive energies */
    force_0[k++] = coheng[i] * eweight;
  }
#ifdef STRESS
  for (i = 0; i < nconf; i++) {	/* then stresses */
    if (usestress[i]) {
      force_0[k++] = stress[i].xx * sweight;
      force_0[k++] = stress[i].yy * sweight;
      force_0[k++] = stress[i].zz * sweight;
      force_0[k++] = stress[i].xy * sweight;
      force_0[k++] = stress[i].yz * sweight;
      force_0[k++] = stress[i].zx * sweight;
    } else {
      for (j = 0; j < 6; j++)
	force_0[k++] = 0.;
    }
  }
#else
  for (i = 0; i < 6 * nconf; i++)
    force_0[k++] = 0.;
#endif /* STRESS */
#ifdef EAM
  for (i = 0; i < nconf; i++)
    force_0[k++] = 0.;		/* punishment rho out of bounds */
  for (i = 0; i < 2 * ntypes; i++) {	/* constraint on U(n=0):=0 */
    /* XXX and U'(n_mean)=0  */
    force_0[k++] = 0.;
  }
#endif

  if (write_pair == 1) {
    char  pairname[255];
    FILE *pairfile;
#ifdef APOT
    int   pair_steps = APOT_STEPS / 2;
#else
    int   pair_steps = 1000 / 2;
#endif
    int   pot_count = ntypes * (ntypes + 1) / 2;
    real  pair_table[pot_count * pair_steps];
    real  pair_dist[pot_count];
    int   pos, max_count = 0;

    strcpy(pairname, config);
    strcat(pairname, ".pair");
    pairfile = fopen(pairname, "w");
    fprintf(pairfile, "# radial distribution file\n");

    for (i = 0; i < pot_count * pair_steps; i++)
      pair_table[i] = 0;

    for (i = 0; i < ntypes; i++)
      for (k = 0; k < ntypes; k++)
#ifdef APOT
	pair_dist[(i <=
		   k) ? i * ntypes + k - (i * (i + 1) / 2) : k * ntypes + i -
		  (k * (k + 1) / 2)] =
	  rcut[i * ntypes + k] * (do_smooth ? CUTOFF_MARGIN : 1) / pair_steps;
#else
	pair_dist[(i <=
		   k) ? i * ntypes + k - (i * (i + 1) / 2) : k * ntypes + i -
		  (k * (k + 1) / 2)] = rcut[i * ntypes + k] / pair_steps;
#endif

    for (k = 0; k < pot_count; k++) {
      for (i = 0; i < natoms; i++)
	for (j = 0; j < atoms[i].n_neigh; j++) {
	  col = (atoms[i].typ <= atoms[i].neigh[j].typ) ?
	    atoms[i].typ * ntypes + atoms[i].neigh[j].typ -
	    ((atoms[i].typ * (atoms[i].typ + 1)) / 2)
	    : atoms[i].neigh[j].typ * ntypes + atoms[i].typ -
	    ((atoms[i].neigh[j].typ * (atoms[i].neigh[j].typ + 1)) / 2);
	  if (col == k) {
	    pos = (int)(atoms[i].neigh[j].r / pair_dist[k]);
#ifdef DEBUG
	    if (atoms[i].neigh[j].r <= 1) {
	      fprintf(stderr, "Short distance (%f) found.\n",atoms[i].neigh[j].r);
	      fprintf(stderr, "\tatom=%d neighbor=%d\n", i, j);
	    }
#endif
	    pair_table[k * pair_steps + pos]++;
	    if (pair_table[k * pair_steps + pos] > max_count)
	      max_count = pair_table[k * pair_steps + pos];
	  }
	}
    }

    for (k = 0; k < pot_count; k++) {
      for (i = 0; i < pair_steps; i++) {
	pair_table[k * pair_steps + i] /= max_count;
	fprintf(pairfile, "%f %f\n", i * pair_dist[k],
		pair_table[k * pair_steps + i]);
      }
      if (k != (pot_count - 1))
	fprintf(pairfile, "\n\n");
    }
    fclose(pairfile);
  }
#ifdef APOT
  for (i = 0; i < opt_pot.ncols; i++) {
    j = i * (i + 1) / 2;
    rmin[j] = mindist[j] * 0.95;
    apot_table.begin[i] = mindist[j] * 0.95;
    opt_pot.begin[i] = mindist[j] * 0.95;
    calc_pot.begin[i] = mindist[j] * 0.95;
  }
  for (i = 0; i < calc_pot.ncols; i++)
    for (j = 0; j < APOT_STEPS; j++) {
      index = i * APOT_STEPS + (i + 1) * 2 + j;
      calc_pot.xcoord[index] = calc_pot.begin[i] + j * calc_pot.step[i];
    }
  update_calc_table(opt_pot.table, calc_pot.table, 1);
#endif

  printf("Minimal Distances Matrix\n");
  printf("Atom\t");
  for (i = 0; i < ntypes; i++)
    printf("%8d\t", i);
  printf("with\n");
  for (i = 0; i < ntypes; i++) {
    printf("%d\t", i);
    for (j = 0; j < ntypes; j++)
      printf("%f\t", mindist[ntypes * i + j]);
    printf("\n");
  }
  free(mindist);

  na_typ = (int **)realloc(na_typ, (nconf + 1) * sizeof(int *));
  if (NULL == na_typ)
    error("Cannot allocate memory for na_typ");
  na_typ[nconf] = (int *)malloc(ntypes * sizeof(int));
  for (i = 0; i < ntypes; i++)
    na_typ[nconf][i] = 0;

  for (i = 0; i < nconf; i++)
    for (j = 0; j < ntypes; j++)
      na_typ[nconf][j] += na_typ[i][j];

  /* print diagnostic message and close file */
  printf("Maximal number of neighbors is %d, MAXNEIGH is %d\n",
	 maxneigh, MAXNEIGH);
  printf("Read %d configurations (%d with forces, %d with stresses)\n",
	 nconf, w_force, w_stress);
  printf("with a total of %d atoms (", natoms);
  for (i = 0; i < ntypes; i++) {
    printf("%d of type %d", na_typ[nconf][i], i);
    if (i != (ntypes - 1))
      printf(", ");
  }
  printf(")\nfrom file %s\n", filename);
  if (sh_dist) {
    sprintf(msg,
	    "Distances too short, last occurence conf %d, see above for details",
	    sh_dist);
    error(msg);
  }
  return;
}

#ifdef APOT

/*******************************************************************************
 *
 * recalculate the slots of the atoms for tabulated potential 
 *
 ******************************************************************************/

void new_slots(int a1)
{
  int   i, j, col, typ1, typ2, a2;
  real  r, rr;

  for (a2 = 0; a2 < pot_list_length[a1]; a2++) {
    i = pot_list[a1][a2][0];
    j = pot_list[a1][a2][1];
    typ1 = atoms[i].typ;
    typ2 = atoms[i].neigh[j].typ;
    col = (typ1 <= typ2) ? typ1 * ntypes + typ2 - ((typ1 * (typ1 + 1)) / 2)
      : typ2 * ntypes + typ1 - ((typ2 * (typ2 + 1)) / 2);
    if (smooth_pot[col] && !invar_pot[col]) {
      r = atoms[i].neigh[j].r;
      if (r < calc_pot.end[col]) {
	rr = r - calc_pot.begin[col];
	atoms[i].neigh[j].slot[0] = (int)(rr * calc_pot.invstep[col]);
	atoms[i].neigh[j].shift[0] =
	  (rr -
	   atoms[i].neigh[j].slot[0] * calc_pot.step[col]) *
	  calc_pot.invstep[col];
	atoms[i].neigh[j].slot[0] += calc_pot.first[col];
	atoms[i].neigh[j].step[0] = calc_pot.step[col];
      }
    }
  }
}

#endif
