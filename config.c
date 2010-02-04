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
* $Revision: 1.66 $
* $Date: 2010/02/04 15:10:45 $
*****************************************************************/

#include "potfit.h"
#include "utils.h"

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
  int   maxneigh = 0, count;
#ifdef APOT
  int   index;
#endif
  int   i, j, k, ix, iy, iz, typ1, typ2, col, slot, klo, khi;
  int   h_stress = 0, h_eng = 0, h_boxx = 0, h_boxy = 0, h_boxz =
    0, use_force;
  int   w_force = 0, w_stress = 0;
  int   tag_format = 0;
  int   sh_dist = 0;		/* short distance flag */
  int   cell_scale[3];
  int   str_len;
  FILE *infile;
  fpos_t filepos;
  char  msg[255], buffer[1024];
  char *res, *ptr;
  atom_t *atom;
  stens *stresses;
  vektor d, dd, iheight;
  real  r, rr, istep, shift, step;
#ifdef MEAM
  int   slot_ij, slot_ik, ijk;
  int   n_j, n_k, nnn;
  real  rjk, rij, rik, ccos, dccos, dc_ij, dc_ik, shift_ij, shift_ik, step_ij,
    step_ik;
  vektor rrjk;
//  FILE        *ang;
#endif

  real *mindist;
  mindist = (real *)malloc(ntypes * ntypes * sizeof(real));
  if (NULL == mindist)
    error("Cannot allocate memory for minimal distance.");
  elements = (char **)malloc(ntypes * sizeof(char *));
  reg_for_free(elements, "elements");
  if (NULL == elements)
    error("Cannot allocate memory for element names.");
  for (i = 0; i < ntypes; i++) {
    elements[i] = (char *)malloc(3 * sizeof(char));
    reg_for_free(elements[i], "elements[i]");
    if (NULL == elements[i]) {
      sprintf(msg, "Cannot allocate memory for element name %d\n", i);
      error(msg);
    }
    sprintf(elements[i], "%d", i);
  }

  for (i = 0; i < ntypes * ntypes; i++)
    mindist[i] = 99;
  for (i = 0; i < ntypes; i++)
    for (j = 0; j < ntypes; j++) {
      k = (i <= j) ? i * ntypes + j - ((i * (i + 1)) / 2) : j * ntypes + i -
	((j * (j + 1)) / 2);
      mindist[k] = MAX(rcut[i * ntypes + j], mindist[i * ntypes + j]);
    }

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
    if (NULL == res) {
      sprintf(msg, "Unexpected end of file in %s", filename);
      error(msg);
    }
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
      if (1 > sscanf(buffer, "%d", &count)) {
	sprintf(msg, "Unexpected end of file in %s", filename);
	error(msg);
      }
    }
    /* increase memory for this many additional atoms */
    atoms = (atom_t *)realloc(atoms, (natoms + count) * sizeof(atom_t));
    if (NULL == atoms)
      error("Cannot allocate memory for atoms");
    coheng = (real *)realloc(coheng, (nconf + 1) * sizeof(real));
    if (NULL == coheng)
      error("Cannot allocate memory for cohesive energy");
    conf_weight = (real *)realloc(conf_weight, (nconf + 1) * sizeof(real));
    if (NULL == conf_weight)
      error("Cannot allocate memory for configuration weights");
    else
      conf_weight[nconf] = 1.;
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
    reg_for_free(na_typ[nconf], "na_typ[nconf]");
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
	} else if (res[1] == 'W') {
	  if (sscanf(res + 3, "%lf\n", &(conf_weight[nconf])) != 1)
	    error("Error in configuration weight\n");
	} else if (res[1] == 'C') {
	  str_len = strlen(res + 3);
	  fgetpos(infile, &filepos);
	  if (!have_elements) {
	    i = -1;
	    do {
	      i++;
	      if ((3 * (i + 1) <= str_len) && ((res + 3 * (i + 1)) != NULL))
		sscanf(res + 3 * (i + 1), "%2s", elements[i]);
	    } while (elements[i][0] != '#' && i < (ntypes - 1));
	    have_elements = 1;
	  } else {
	    i = -1;
	    do {
	      i++;
	      if ((3 * (i + 1) <= str_len) && ((res + 3 * (i + 1)) != NULL))
		sscanf(res + 3 * (i + 1), "%s", msg);
	      if (strlen(res + 3 * (i + 1)) == 0)
		break;
	      if (strcmp(msg, elements[i]) != 0) {
		if (atoi(elements[i]) == i && atoi(elements[i]) != 0) {
		  strcpy(elements[i], msg);
		} else {
		  fprintf(stderr,
			  " --> Warning <--\nFound element mismatch in configuration file!\n");
		  if ((ptr = strchr(msg, '\n')) != NULL)
		    *ptr = '\0';
		  fprintf(stderr, "Mismatch found in configuration %d.\n",
			  nconf);
		  strncpy(msg, res + 3 * (i + 1), 2);
		  msg[2] = '\0';
		  fprintf(stderr,
			  "Expected element >> %s << but found element >> %s <<.\n",
			  elements[i], msg);
		  fprintf(stderr,
			  "You can use list_config to identify that configuration.\n\n");
		  error("Please check your configuration files!");
		}
	      }
	    } while (elements[i][0] != '#' && i < (ntypes - 1));
	  }
	  fsetpos(infile, &filepos);
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
    /* check cell size */
    /* inverse height in direction */
    iheight.x = sqrt(SPROD(tbox_x, tbox_x));
    iheight.y = sqrt(SPROD(tbox_y, tbox_y));
    iheight.z = sqrt(SPROD(tbox_z, tbox_z));

    if ((ceil(rcutmax * iheight.x) > 30000) ||
	(ceil(rcutmax * iheight.y) > 30000) ||
	(ceil(rcutmax * iheight.z) > 30000))
      error("Very bizarre small cell size - aborting");

    cell_scale[0] = (int)ceil(rcutmax * iheight.x);
    cell_scale[1] = (int)ceil(rcutmax * iheight.y);
    cell_scale[2] = (int)ceil(rcutmax * iheight.z);

#ifdef DEBUG
    fprintf(stderr, "Checking cell size for configuration %d:\n", nconf + 1);
    fprintf(stderr, "Box dimensions:\n");
    fprintf(stderr, "     %10.6f %10.6f %10.6f\n", box_x.x, box_x.y, box_x.z);
    fprintf(stderr, "     %10.6f %10.6f %10.6f\n", box_y.x, box_y.y, box_y.z);
    fprintf(stderr, "     %10.6f %10.6f %10.6f\n", box_z.x, box_z.y, box_z.z);
    fprintf(stderr, "Box normals:\n");
    fprintf(stderr, "     %10.6f %10.6f %10.6f\n", tbox_x.x, tbox_x.y,
	    tbox_x.z);
    fprintf(stderr, "     %10.6f %10.6f %10.6f\n", tbox_y.x, tbox_y.y,
	    tbox_y.z);
    fprintf(stderr, "     %10.6f %10.6f %10.6f\n", tbox_z.x, tbox_z.y,
	    tbox_z.z);
    fprintf(stderr, "Box heights:\n");
    fprintf(stderr, "     %10.6f %10.6f %10.6f\n",
	    1. / iheight.x, 1. / iheight.y, 1. / iheight.z);
    fprintf(stderr, "Potential range:  %f\n", rcutmax);
    fprintf(stderr, "Periodic images needed: %d %d %d\n\n",
	    2 * cell_scale[0] + 1, 2 * cell_scale[1] + 1,
	    2 * cell_scale[2] + 1);
#endif /* DEBUG */

    /* compute the neighbor table */
    for (i = natoms; i < natoms + count; i++) {
      atoms[i].n_neigh = 0;
      for (j = natoms; j < natoms + count; j++) {
	d.x = atoms[j].pos.x - atoms[i].pos.x;
	d.y = atoms[j].pos.y - atoms[i].pos.y;
	d.z = atoms[j].pos.z - atoms[i].pos.z;
	for (ix = -cell_scale[0]; ix <= cell_scale[0]; ix++)
	  for (iy = -cell_scale[1]; iy <= cell_scale[1]; iy++)
	    for (iz = -cell_scale[2]; iz <= cell_scale[2]; iz++) {
	      if ((i == j) && (ix == 0) && (iy == 0) && (iz == 0))
		continue;
	      dd.x = d.x + ix * box_x.x + iy * box_y.x + iz * box_z.x;
	      dd.y = d.y + ix * box_x.y + iy * box_y.y + iz * box_z.y;
	      dd.z = d.z + ix * box_x.z + iy * box_y.z + iz * box_z.z;
	      r = sqrt(SPROD(dd, dd));
	      typ1 = atoms[i].typ;
	      typ2 = atoms[j].typ;
	      if (r <= rcut[typ1 * ntypes + typ2]) {
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

		col = (typ1 <= typ2) ?
		  typ1 * ntypes + typ2 - ((typ1 * (typ1 + 1)) / 2)
		  : typ2 * ntypes + typ1 - ((typ2 * (typ2 + 1)) / 2);
		mindist[col] = MIN(mindist[col], r);

		/* pre-compute index and shift into potential table */
		/* pair potential */
		if (!sh_dist) {
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
		    pot_list[col] =
		      (int **)realloc(pot_list[col],
				      (pot_list_length[col] +
				       1) * sizeof(int *));
		    pot_list[col][pot_list_length[col]] =
		      (int *)malloc(2 * sizeof(int));
/*                    reg_for_free(pot_list[col][pot_list_length[col]]);*/
		    pot_list[col][pot_list_length[col]][0] = i;
		    pot_list[col][pot_list_length[col]][1] = k;
		    pot_list_length[col]++;
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
#if defined EAM || defined MEAM
		  /* EAM-MEAM part */
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
	      }
	    }
      }
      maxneigh = MAX(maxneigh, atoms[i].n_neigh);
    }
    /* compute the angl part */
#ifdef MEAM
    for (i = natoms; i < natoms + count; i++) {
      nnn = atoms[i].n_neigh;
      ijk = 0;
      for (j = 0; j < nnn - 1; j++) {
	for (k = j + 1; k < nnn; k++) {
	  atoms[i].angl_part[ijk].typ2 = atoms[i].neigh[j].typ;
	  atoms[i].angl_part[ijk].typ3 = atoms[i].neigh[k].typ;
	  atoms[i].angl_part[ijk].nr2 = atoms[i].neigh[j].nr;
	  n_j = atoms[i].angl_part[ijk].nr2;
	  atoms[i].angl_part[ijk].nr3 = atoms[i].neigh[k].nr;
	  n_k = atoms[i].angl_part[ijk].nr3;
	  atoms[i].angl_part[ijk].r2 = atoms[i].neigh[j].r;
	  atoms[i].angl_part[ijk].dist_ij = atoms[i].neigh[j].dist;
	  atoms[i].angl_part[ijk].dist_ik = atoms[i].neigh[k].dist;
	  rij = atoms[i].neigh[j].r;
	  atoms[i].angl_part[ijk].r3 = atoms[i].neigh[k].r;
	  rik = atoms[i].neigh[k].r;
	  rrjk.x =
	    atoms[i].neigh[j].dist.x * rij - atoms[i].neigh[k].dist.x * rik;
	  rrjk.y =
	    atoms[i].neigh[j].dist.y * rij - atoms[i].neigh[k].dist.y * rik;
	  rrjk.z =
	    atoms[i].neigh[j].dist.z * rij - atoms[i].neigh[k].dist.z * rik;
	  rjk = sqrt(SPROD(rrjk, rrjk));
	  ccos = (rij * rij + rik * rik - rjk * rjk) / (2 * rij * rik);
	  dccos =
	    (rij * rij * rik -
	     (rik * rik - rjk * rjk) * rik) / (2 * rij * rij * rik * rik);
	  atoms[i].angl_part[ijk].cos = ccos;
	  atoms[i].angl_part[ijk].dcos_ij = dccos;
	  dccos =
	    (rik * rik * rij -
	     (rij * rij - rjk * rjk) * rij) / (2 * rij * rij * rik * rik);
	  atoms[i].angl_part[ijk].dcos_ik = dccos;	/* three different angular derivativies are used at calculation of force */
	  dccos = (rjk) / (rij * rij) * rrjk.x / rjk;
	  atoms[i].angl_part[ijk].dcos_jk_x = dccos;	/* three different angular derivativies are used at calculation of force */
	  dccos = (rjk) / (rij * rij) * rrjk.y / rjk;
	  atoms[i].angl_part[ijk].dcos_jk_y = dccos;
	  dccos = (rjk) / (rij * rij) * rrjk.z / rjk;
	  atoms[i].angl_part[ijk].dcos_jk_z = dccos;
	  dc_ij = atoms[i].angl_part[ijk].dcos_ij;
	  dc_ik = atoms[i].angl_part[ijk].dcos_ik;
	  col = 2 * paircol + 2 * ntypes + atoms[i].typ;
	  if (format == 3) {
	    if (ccos > 1) {
	      printf("%f %f %d %d %d\n", r, calc_pot.begin[col], col,
		     typ1, typ2);
	      fflush(stdout);
	      error("wrong cos, it is strange!");
	    }
	    istep = calc_pot.invstep[col];
	    slot = (int)((ccos + 1) * istep);
	    shift = ((ccos + 1) - slot * calc_pot.step[col]) * istep;
	    slot += calc_pot.first[col];
	    step = calc_pot.step[col];
	  }
	  col = (atoms[i].typ <= atoms[i].angl_part[ijk].typ2) ?
	    paircol + 2 * ntypes + atoms[i].typ * ntypes +
	    atoms[i].angl_part[ijk].typ2 -
	    ((atoms[i].typ * (atoms[i].typ + 1)) / 2)
	    : paircol + 2 * ntypes + atoms[i].angl_part[ijk].typ2 * ntypes +
	    atoms[i].typ -
	    ((atoms[i].angl_part[ijk].typ2 *
	      (atoms[i].angl_part[ijk].typ2 + 1)) / 2);
	  if (format == 3) {
	    rr = rij - calc_pot.begin[col];
	    if (rr < 0) {
	      printf("%f %f %d %d %d\n", rij, calc_pot.begin[col], col,
		     atoms[i].typ, atoms[i].angl_part[ijk].typ2);
	      fflush(stdout);
	      error("short distance in config.c!");
	    }
	    istep = calc_pot.invstep[col];
	    slot_ij = (int)(rr * istep);
	    shift_ij = (rr - slot_ij * calc_pot.step[col]) * istep;
	    slot_ij += calc_pot.first[col];
	    step_ij = calc_pot.step[col];
	  }
	  col = (atoms[i].typ <= atoms[i].angl_part[ijk].typ3) ?
	    paircol + 2 * ntypes + atoms[i].typ * ntypes +
	    atoms[i].angl_part[ijk].typ3 -
	    ((atoms[i].typ * (atoms[i].typ + 1)) / 2)
	    : paircol + 2 * ntypes + atoms[i].angl_part[ijk].typ3 * ntypes +
	    atoms[i].typ -
	    ((atoms[i].angl_part[ijk].typ3 *
	      (atoms[i].angl_part[ijk].typ3 + 1)) / 2);
	  if (format == 3) {
	    rr = rik - calc_pot.begin[col];
	    if (rr < 0) {
	      printf("%f %f %d %d %d\n", rik, calc_pot.begin[col], col,
		     atoms[i].typ, atoms[i].angl_part[ijk].typ3);
	      fflush(stdout);
	      error("short distance in config.c!");
	    }
	    istep = calc_pot.invstep[col];
	    slot_ik = (int)(rr * istep);
	    shift_ik = (rr - slot_ik * calc_pot.step[col]) * istep;
	    slot_ik += calc_pot.first[col];
	    step_ik = calc_pot.step[col];
	  }
	  atoms[i].angl_part[ijk].shift[0] = shift_ij;
	  atoms[i].angl_part[ijk].slot[0] = slot_ij;
	  atoms[i].angl_part[ijk].step[0] = step_ij;
	  atoms[i].angl_part[ijk].shift[1] = shift_ik;
	  atoms[i].angl_part[ijk].slot[1] = slot_ik;
	  atoms[i].angl_part[ijk].step[1] = step_ik;
	  atoms[i].angl_part[ijk].shift[2] = shift;
	  atoms[i].angl_part[ijk].slot[2] = slot;
	  atoms[i].angl_part[ijk].step[2] = step;
//        ang = fopen("/home/sstar/potfit/ang.txt", "a");
//        fclose(ang);
	  ijk++;
	}
      }
    }
#endif /* MEAM */
/* increment natoms and configuration number */
    natoms += count;
    nconf++;

  } while (!feof(infile));
  fclose(infile);

  reg_for_free(atoms, "atoms");
  reg_for_free(coheng, "coheng");
  reg_for_free(volumen, "volumen");
  reg_for_free(stress, "stress");
  reg_for_free(inconf, "inconf");
  reg_for_free(cnfstart, "cnfstart");
  reg_for_free(useforce, "useforce");
  reg_for_free(usestress, "usestress");

  mdim = 3 * natoms + 7 * nconf;	/* mdim is dimension of force vector
					   3*natoms are real forces,
					   nconf cohesive energies,
					   6*nconf stress tensor components */
#if defined EAM || defined MEAM
  mdim += nconf;		/* nconf limiting constraints */
  mdim += 2 * ntypes;		/* ntypes dummy constraints */
#endif /* EAM MEAM */
#ifdef APOT
  mdim += opt_pot.idxlen;	/* 1 slot for each analytic parameter -> punishment */
  mdim += apot_table.number + 1;	/* 1 slot for each analytic potential -> punishment */
#endif /* APOT */
  /* copy forces into single vector */
  if (NULL == (force_0 = (real *)malloc(mdim * sizeof(real))))
    error("Cannot allocate forces");
  reg_for_free(force_0, "force_0");
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
#if defined EAM || defined MEAM
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
    real  pair_table[paircol * pair_steps];
    real  pair_dist[paircol];
    int   pos, max_count = 0;

    strcpy(pairname, config);
    strcat(pairname, ".pair");
    pairfile = fopen(pairname, "w");
    fprintf(pairfile, "# radial distribution file for %d potential(s)\n",
	    paircol);

    for (i = 0; i < paircol * pair_steps; i++)
      pair_table[i] = 0;

    for (i = 0; i < ntypes; i++)
      for (k = 0; k < ntypes; k++)
	pair_dist[(i <=
		   k) ? i * ntypes + k - (i * (i + 1) / 2) : k * ntypes +
		  i - (k * (k + 1) / 2)] = rcut[i * ntypes + k] / pair_steps;

    for (k = 0; k < paircol; k++) {
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
	      fprintf(stderr, "Short distance (%f) found.\n",
		      atoms[i].neigh[j].r);
	      fprintf(stderr, "\tatom=%d neighbor=%d\n", i, j);
	    }
#endif
	    pair_table[k * pair_steps + pos]++;
	    if (pair_table[k * pair_steps + pos] > max_count)
	      max_count = (int)pair_table[k * pair_steps + pos];
	  }
	}
    }

    for (k = 0; k < paircol; k++) {
      for (i = 0; i < pair_steps; i++) {
	pair_table[k * pair_steps + i] /= (max_count * 10 / 6);
	fprintf(pairfile, "%f %f\n", i * pair_dist[k],
		pair_table[k * pair_steps + i]);
      }
      if (k != (paircol - 1))
	fprintf(pairfile, "\n\n");
    }
    fclose(pairfile);
  }
#ifdef APOT
  real  min = 10.;

  for (i = 0; i < ntypes; i++)
    for (j = 0; j < ntypes; j++) {
      k = (i <= j) ? i * ntypes + j - ((i * (i + 1)) / 2) : j * ntypes + i -
	((j * (j + 1)) / 2);
      if (mindist[k] == 99)
	mindist[k] = 3;
      rmin[i * ntypes + j] = mindist[k];
      apot_table.begin[k] = mindist[k] * 0.95;
      opt_pot.begin[k] = mindist[k] * 0.95;
      calc_pot.begin[k] = mindist[k] * 0.95;
      min = MIN(min, mindist[k]);
    }
#ifdef EAM
  for (i = 0; i < ntypes; i++) {
    j = i + ntypes * (ntypes + 1) / 2;
    apot_table.begin[j] = min * 0.95;
    opt_pot.begin[j] = min * 0.95;
    calc_pot.begin[j] = min * 0.95;
  }
#endif
  for (i = 0; i < calc_pot.ncols; i++) {
    calc_pot.step[i] =
      (calc_pot.end[i] - calc_pot.begin[i]) / (APOT_STEPS - 1);
    calc_pot.invstep[i] = 1. / calc_pot.step[i];
    for (j = 0; j < APOT_STEPS; j++) {
      index = i * APOT_STEPS + (i + 1) * 2 + j;
      calc_pot.xcoord[index] = calc_pot.begin[i] + j * calc_pot.step[i];
    }
  }
#endif

  printf("Minimal Distances Matrix\n");
  printf("Atom\t");
  for (i = 0; i < ntypes; i++)
    printf("%8s\t", elements[i]);
  printf("with\n");
  for (i = 0; i < ntypes; i++) {
    printf("%s\t", elements[i]);
    for (j = 0; j < ntypes; j++) {
      k = (i <= j) ? i * ntypes + j - ((i * (i + 1)) / 2) : j * ntypes + i -
	((j * (j + 1)) / 2);
      printf("%f\t", mindist[k]);

    }
    printf("\n");
  }

  free(mindist);

  na_typ = (int **)realloc(na_typ, (nconf + 1) * sizeof(int *));
  reg_for_free(na_typ, "na_typ");
  if (NULL == na_typ)
    error("Cannot allocate memory for na_typ");
  na_typ[nconf] = (int *)malloc(ntypes * sizeof(int));
  reg_for_free(na_typ[nconf], "na_typ[nconf]");
  for (i = 0; i < ntypes; i++)
    na_typ[nconf][i] = 0;

  for (i = 0; i < nconf; i++)
    for (j = 0; j < ntypes; j++)
      na_typ[nconf][j] += na_typ[i][j];

  /* print diagnostic message and close file */
  printf("Maximum number of neighbors is %d, MAXNEIGH is %d\n",
	 maxneigh, MAXNEIGH);
  printf("Read %d configurations (%d with forces, %d with stresses)\n",
	 nconf, w_force, w_stress);
  printf("with a total of %d atoms (", natoms);
  for (i = 0; i < ntypes; i++) {
    if (have_elements)
      printf("%d %s (%.2f%%)", na_typ[nconf][i], elements[i],
	     100. * na_typ[nconf][i] / natoms);
    else
      printf("%d type %d (%.2f%%)", na_typ[nconf][i], i,
	     100. * na_typ[nconf][i] / natoms);
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

void new_slots(int a1, int force_update)
{
  int   i, j, col, col2, typ1, typ2, a2;
  real  r, rr;
  atom_t *atom;

  for (a2 = 0; a2 < pot_list_length[a1]; a2++) {
    atom = conf_atoms - firstatom;
    i = pot_list[a1][a2][0];
    j = pot_list[a1][a2][1];
    if (i >= firstatom && i < (firstatom + myatoms)) {
      atom += i;
      if (&atom->typ != NULL) {
	typ1 = atom->typ;
	typ2 = atom->neigh[j].typ;
	col =
	  (typ1 <= typ2) ? typ1 * ntypes + typ2 - ((typ1 * (typ1 + 1)) / 2)
	  : typ2 * ntypes + typ1 - ((typ2 * (typ2 + 1)) / 2);
	if (force_update || smooth_pot[col]) {
	  r = atom->neigh[j].r;
	  if (r < calc_pot.end[col]) {
	    rr = r - calc_pot.begin[col];
	    atom->neigh[j].slot[0] = (int)(rr * calc_pot.invstep[col]);
	    atom->neigh[j].step[0] = calc_pot.step[col];
	    atom->neigh[j].shift[0] =
	      (rr -
	       atom->neigh[j].slot[0] * calc_pot.step[col]) *
	      calc_pot.invstep[col];
#ifdef EAM
	    col2 = paircol + typ2;
	    rr = r - calc_pot.begin[col2];
	    atom->neigh[j].slot[1] = (int)(rr * calc_pot.invstep[col2]);
	    atom->neigh[j].step[1] = calc_pot.step[col2];
	    atom->neigh[j].shift[1] =
	      (rr -
	       atom->neigh[j].slot[1] * calc_pot.step[col2]) *
	      calc_pot.invstep[col2];
	    atom->neigh[j].slot[1] += calc_pot.first[col2];
#endif
	    atom->neigh[j].slot[0] += calc_pot.first[col];
	    atom->neigh[j].step[0] = calc_pot.step[col];
	  }
	}
      }
    }
  }
}

#endif
