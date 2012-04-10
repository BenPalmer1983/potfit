/****************************************************************
 *
 * config.c: Reads atomic configurations and forces.
 *
 ****************************************************************
 *
 * Copyright 2002-2012
 *	Institute for Theoretical and Applied Physics
 *	University of Stuttgart, D-70550 Stuttgart, Germany
 *	http://potfit.itap.physik.uni-stuttgart.de/
 *
 ****************************************************************
 *
 *   This file is part of potfit.
 *
 *   potfit is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   potfit is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with potfit; if not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************/

#include "potfit.h"

#include "config.h"
#include "utils.h"

/****************************************************************
 *
 *  read the configurations
 *
 ****************************************************************/

void read_config(char *filename)
{
  atom_t *atom;
  char  msg[255], buffer[1024];
  char *res, *ptr;
  char *tmp, *res_tmp;
  int   count;
  int   i, j, k, ix, iy, iz;
  int   typ1, typ2, col, slot, klo, khi;
  int   cell_scale[3];
  int   fixed_elements;
  int   h_stress = 0, h_eng = 0, h_boxx = 0, h_boxy = 0, h_boxz = 0, use_force;
#ifdef CONTRIB
  int   have_contrib = 0;
#endif /* CONTRIB */
  int   line = 0;
  int   max_type = 0;
  int   sh_dist = 0;		/* short distance flag */
  int   str_len;
  int   tag_format = 0;
  int   w_force = 0, w_stress = 0;
#ifdef APOT
  int   index;
#endif /* APOT */
  FILE *infile;
  fpos_t filepos;
  double r, rr, istep, shift, step;
  double *mindist;
  sym_tens *stresses;
  vector d, dd, iheight;

  /* initialize elements array */
  elements = (char **)malloc(ntypes * sizeof(char *));
  if (NULL == elements)
    error(1, "Cannot allocate memory for element names.");
  reg_for_free(elements, "elements");
  for (i = 0; i < ntypes; i++) {
    elements[i] = (char *)malloc(3 * sizeof(char));
    if (NULL == elements[i])
      error(1, "Cannot allocate memory for %d. element name.\n", i + 1);
    reg_for_free(elements[i], "elements[%d]", i);
    snprintf(elements[i], 3, "%d\0", i);
  }

  /* initialize minimum distance array */
  mindist = (double *)malloc(ntypes * ntypes * sizeof(double));
  if (NULL == mindist)
    error(1, "Cannot allocate memory for minimal distance.");

  /* set maximum cutoff distance as starting value for mindist */
  for (i = 0; i < ntypes * ntypes; i++)
    mindist[i] = 99.;
  for (i = 0; i < ntypes; i++)
    for (j = 0; j < ntypes; j++) {
      k =
	(i <=
	j) ? i * ntypes + j - ((i * (i + 1)) / 2) : j * ntypes + i - ((j * (j +
	    1)) / 2);
      mindist[k] = MAX(rcut[i * ntypes + j], mindist[i * ntypes + j]);
    }

  nconf = 0;

  /* open file */
  infile = fopen(filename, "r");
  if (NULL == infile)
    error(1, "Could not open file %s\n", filename);

  /* read configurations until the end of the file */
  do {
    res = fgets(buffer, 1024, infile);
    line++;
    if (NULL == res)
      error(1, "Unexpected end of file in %s", filename);
    if (res[0] == '#') {	/* new file type */
      tag_format = 1;
      h_eng = h_stress = h_boxx = h_boxy = h_boxz = 0;
      if (res[1] == 'N') {	/* Atom number line */
	if (sscanf(res + 3, "%d %d", &count, &use_force) < 2)
	  error(1, "%s: Error in atom number specification on line %d\n",
	    filename, line);
      } else
	error(1, "%s: Error - number of atoms missing on line %d\n", filename,
	  line);
    } else {
      /* number of atoms in this configuration */
      tag_format = 0;
      use_force = 1;
      if (1 > sscanf(buffer, "%d", &count))
	error(1, "Unexpected end of file in %s", filename);
    }
    /* increase memory for this many additional atoms */
    atoms = (atom_t *)realloc(atoms, (natoms + count) * sizeof(atom_t));
    if (NULL == atoms)
      error(1, "Cannot allocate memory for atoms");
    for (i = 0; i < count; i++)
      atoms[natoms + i].neigh = malloc(1 * sizeof(neigh_t));
    coheng = (double *)realloc(coheng, (nconf + 1) * sizeof(double));
    if (NULL == coheng)
      error(1, "Cannot allocate memory for cohesive energy");
    conf_weight = (double *)realloc(conf_weight, (nconf + 1) * sizeof(double));
    if (NULL == conf_weight)
      error(1, "Cannot allocate memory for configuration weights");
    else
      conf_weight[nconf] = 1.;
    volumen = (double *)realloc(volumen, (nconf + 1) * sizeof(double));
    if (NULL == volumen)
      error(1, "Cannot allocate memory for volume");
    stress = (sym_tens *)realloc(stress, (nconf + 1) * sizeof(sym_tens));
    if (NULL == stress)
      error(1, "Cannot allocate memory for stress");
    inconf = (int *)realloc(inconf, (nconf + 1) * sizeof(int));
    if (NULL == inconf)
      error(1, "Cannot allocate memory for atoms in conf");
    cnfstart = (int *)realloc(cnfstart, (nconf + 1) * sizeof(int));
    if (NULL == cnfstart)
      error(1, "Cannot allocate memory for start of conf");
    useforce = (int *)realloc(useforce, (nconf + 1) * sizeof(int));
    if (NULL == useforce)
      error(1, "Cannot allocate memory for useforce");
    usestress = (int *)realloc(usestress, (nconf + 1) * sizeof(int));
    if (NULL == useforce)
      error(1, "Cannot allocate memory for usestress");
    na_type = (int **)realloc(na_type, (nconf + 2) * sizeof(int *));
    if (NULL == na_type)
      error(1, "Cannot allocate memory for na_type");
    na_type[nconf] = (int *)malloc(ntypes * sizeof(int));
    reg_for_free(na_type[nconf], "na_type[%d]", nconf);
    if (NULL == na_type[nconf])
      error(1, "Cannot allocate memory for na_type");

    for (i = 0; i < ntypes; i++)
      na_type[nconf][i] = 0;

    inconf[nconf] = count;
    cnfstart[nconf] = natoms;
    useforce[nconf] = use_force;
    stresses = stress + nconf;
#ifdef CONTRIB
    have_contrib = 0;
    have_contrib_box = 0;
#endif /* CONTRIB */

    if (tag_format) {
      do {
	res = fgets(buffer, 1024, infile);
	if ((ptr = strchr(res, '\n')) != NULL)
	  *ptr = '\0';
	line++;
	/* read the box vectors */
	if (res[1] == 'X') {
	  if (sscanf(res + 3, "%lf %lf %lf\n", &box_x.x, &box_x.y,
	      &box_x.z) == 3)
	    h_boxx++;
	  else
	    error(1, "%s: Error in box vector x, line %d\n", filename, line);
	} else if (res[1] == 'Y') {
	  if (sscanf(res + 3, "%lf %lf %lf\n", &box_y.x, &box_y.y,
	      &box_y.z) == 3)
	    h_boxy++;
	  else
	    error(1, "%s: Error in box vector y, line %d\n", filename, line);
	} else if (res[1] == 'Z') {
	  if (sscanf(res + 3, "%lf %lf %lf\n", &box_z.x, &box_z.y,
	      &box_z.z) == 3)
	    h_boxz++;
	  else
	    error(1, "%s: Error in box vector z, line %d\n", filename, line);
#ifdef CONTRIB
	} else if (strncmp(res + 1, "B_O", 3) == 0) {
	  if (1 == have_contrib_box) {
	    error(0, "There can only be one box of contributing atoms\n");
	    error(1, "This occured in %s on line %d", filename, line);
	  }
	  if (sscanf(res + 5, "%lf %lf %lf\n", &cbox_o.x, &cbox_o.y,
	      &cbox_o.z) == 3) {
	    have_contrib_box = 1;
	    have_contrib++;
	  } else
	    error(1, "%s: Error in box of contributing atoms, line %d\n",
	      filename, line);
	} else if (strncmp(res + 1, "B_A", 3) == 0) {
	  if (sscanf(res + 5, "%lf %lf %lf\n", &cbox_a.x, &cbox_a.y,
	      &cbox_a.z) == 3) {
	    have_contrib++;
	  } else
	    error(1, "%s: Error in box of contributing atoms, line %d\n",
	      filename, line);
	} else if (strncmp(res + 1, "B_B", 3) == 0) {
	  if (sscanf(res + 5, "%lf %lf %lf\n", &cbox_b.x, &cbox_b.y,
	      &cbox_b.z) == 3) {
	    have_contrib++;
	  } else
	    error(1, "%s: Error in box of contributing atoms, line %d\n",
	      filename, line);
	} else if (strncmp(res + 1, "B_C", 3) == 0) {
	  if (sscanf(res + 5, "%lf %lf %lf\n", &cbox_c.x, &cbox_c.y,
	      &cbox_c.z) == 3) {
	    have_contrib++;
	  } else
	    error(1, "%s: Error in box of contributing atoms, line %d\n",
	      filename, line);
	} else if (strncmp(res + 1, "B_S", 3) == 0) {
	  sphere_centers =
	    (vector *)realloc(sphere_centers, (n_spheres + 1) * sizeof(vector));
	  r_spheres =
	    (double *)realloc(r_spheres, (n_spheres + 1) * sizeof(double));
	  if (sscanf(res + 5, "%lf %lf %lf %lf\n", &sphere_centers[n_spheres].x,
	      &sphere_centers[n_spheres].y, &sphere_centers[n_spheres].z,
	      &r_spheres[n_spheres]) == 4) {
	    n_spheres++;
	  } else
	    error(1, "%s: Error in sphere of contributing atoms, line %d\n",
	      filename, line);
#endif /* CONTRIB */
	} else if (res[1] == 'E') {
	  if (sscanf(res + 3, "%lf\n", &(coheng[nconf])) == 1)
	    h_eng++;
	  else
	    error(1, "%s: Error in energy on line %d\n", filename, line);
	} else if (res[1] == 'W') {
	  if (sscanf(res + 3, "%lf\n", &(conf_weight[nconf])) != 1)
	    error(1, "%s: Error in configuration weight on line %d\n", filename,
	      line);
	} else if (res[1] == 'C') {
	  fgetpos(infile, &filepos);
	  if (!have_elements) {
	    i = 0;
	    for (j = 0; j < ntypes; j++) {
	      res_tmp = res + 3 + i;
	      if (strchr(res_tmp, ' ') != NULL && strlen(res_tmp) > 0) {
		tmp = strchr(res_tmp, ' ');
		str_len = tmp - res_tmp + 1;
		strncpy(elements[j], res_tmp, str_len - 1);
		elements[j][str_len - 1] = '\0';
		i += str_len;
	      } else if (strlen(res_tmp) >= 1) {
		if ((ptr = strchr(res_tmp, '\n')) != NULL)
		  *ptr = '\0';
		strcpy(elements[j], res_tmp);
		i += strlen(res_tmp);
		fixed_elements = j;
	      } else
		break;
	    }
	    have_elements = 1;
	  } else {
	    i = 0;
	    for (j = 0; j < ntypes; j++) {
	      res_tmp = res + 3 + i;
	      if (strchr(res_tmp, ' ') != NULL && strlen(res_tmp) > 0) {
		/* more than one element left */
		tmp = strchr(res_tmp, ' ');
		str_len = tmp - res_tmp + 1;
		strncpy(msg, res_tmp, str_len - 1);
		msg[str_len - 1] = '\0';
		i += str_len;
		if (strcmp(msg, elements[j]) != 0) {
		  if (atoi(elements[j]) == j && j > fixed_elements) {
		    strcpy(elements[j], msg);
		    fixed_elements++;
		  } else {
		    /* Fix newline at the end of a string */
		    if ((ptr = strchr(msg, '\n')) != NULL)
		      *ptr = '\0';
		    error(0, "Mismatch found in configuration %d, line %d.\n",
		      nconf, line);
		    error(0,
		      "Expected element >> %s << but found element >> %s <<.\n",
		      elements[j], msg);
		    error(0,
		      "You can use list_config to identify that configuration.\n");
		    error(1, "Please check your configuration files!\n");
		  }
		}
	      } else if (strlen(res_tmp) > 1) {
		strcpy(msg, res_tmp);
		if ((ptr = strchr(msg, '\n')) != NULL)
		  *ptr = '\0';
		i += strlen(msg);
		if (strcmp(msg, elements[j]) != 0) {
		  if (atoi(elements[j]) == j && j > fixed_elements) {
		    strcpy(elements[j], msg);
		    fixed_elements++;
		  } else {
		    /* Fix newline at the end of a string */
		    if ((ptr = strchr(msg, '\n')) != NULL)
		      *ptr = '\0';
		    error(0, "Mismatch found in configuration %d on line %d.\n",
		      nconf, line);
		    error(0,
		      "Expected element >> %s << but found element >> %s <<.\n",
		      elements[j], msg);
		    error(0,
		      "You can use list_config to identify that configuration.\n");
		    error(1, "Please check your configuration files!");
		  }
		}
	      } else
		break;
	    }
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
	    error(1, "Error in stress tensor on line %d\n", line);
	}
      } while (res[1] != 'F');
      if (!(h_eng && h_boxx && h_boxy && h_boxz))
	error(1, "Incomplete box vectors for config %d!", nconf);
#ifdef CONTRIB
      if (have_contrib_box && have_contrib != 4)
	error(1, "Incomplete box of contributing atoms for config %d!", nconf);
#endif /* CONTRIB */
      usestress[nconf] = h_stress;	/* no stress tensor available */
    } else {
      /* read the box vectors */
      fscanf(infile, "%lf %lf %lf\n", &box_x.x, &box_x.y, &box_x.z);
      fscanf(infile, "%lf %lf %lf\n", &box_y.x, &box_y.y, &box_y.z);
      fscanf(infile, "%lf %lf %lf\n", &box_z.x, &box_z.y, &box_z.z);
      line += 3;

      /* read cohesive energy */
      if (1 != fscanf(infile, "%lf\n", &(coheng[nconf])))
	error(1, "Configuration file without cohesive energy -- old format!");
      line++;

      /* read stress tensor */
      if (6 != fscanf(infile, "%lf %lf %lf %lf %lf %lf\n", &(stresses->xx),
	  &(stresses->yy), &(stresses->zz), &(stresses->xy), &(stresses->yz),
	  &(stresses->zx)))
	error(1, "No stresses given -- old format");
      usestress[nconf] = 1;
      line++;
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
	  &(atom->pos.x), &(atom->pos.y), &(atom->pos.z), &(atom->force.x),
	  &(atom->force.y), &(atom->force.z)))
	error(1, "Corrupt configuration file on line %d\n", line + 1);
      line++;
      if (atom->typ >= ntypes || atom->typ < 0)
	error(1,
	  "Corrupt configuration file on line %d: Incorrect atom type (%d)\n",
	  line, atom->typ);
      atom->absforce =
	sqrt(dsquare(atom->force.x) + dsquare(atom->force.y) +
	dsquare(atom->force.z));
      atom->conf = nconf;
#ifdef CONTRIB
      if (have_contrib_box || n_spheres != 0)
	atom->contrib = does_contribute(atom->pos);
      else
	atom->contrib = 1;
#endif
      na_type[nconf][atom->typ] += 1;
      max_type = MAX(max_type, atom->typ);
    }
    /* check cell size */
    /* inverse height in direction */
    iheight.x = sqrt(SPROD(tbox_x, tbox_x));
    iheight.y = sqrt(SPROD(tbox_y, tbox_y));
    iheight.z = sqrt(SPROD(tbox_z, tbox_z));

    if ((ceil(rcutmax * iheight.x) > 30000)
      || (ceil(rcutmax * iheight.y) > 30000)
      || (ceil(rcutmax * iheight.z) > 30000))
      error(1, "Very bizarre small cell size - aborting");

    cell_scale[0] = (int)ceil(rcutmax * iheight.x);
    cell_scale[1] = (int)ceil(rcutmax * iheight.y);
    cell_scale[2] = (int)ceil(rcutmax * iheight.z);

#ifdef DEBUG
    fprintf(stderr, "Checking cell size for configuration %d:\n", nconf);
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
    fprintf(stderr, "     %10.6f %10.6f %10.6f\n", 1. / iheight.x,
      1. / iheight.y, 1. / iheight.z);
    fprintf(stderr, "Potential range:  %f\n", rcutmax);
    fprintf(stderr, "Periodic images needed: %d %d %d\n\n",
      2 * cell_scale[0] + 1, 2 * cell_scale[1] + 1, 2 * cell_scale[2] + 1);
#endif /* DEBUG */

    /* compute the neighbor table */
    for (i = natoms; i < natoms + count; i++) {
      atoms[i].n_neigh = 0;
      for (j = i; j < natoms + count; j++) {
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
		  fprintf(stderr, "Configuration %d: Distance %f\n", nconf, r);
		  fprintf(stderr, "atom %d (type %d) at pos: %f %f %f\n",
		    i - natoms, typ1, atoms[i].pos.x, atoms[i].pos.y,
		    atoms[i].pos.z);
		  fprintf(stderr, "atom %d (type %d) at pos: %f %f %f\n",
		    j - natoms, typ2, dd.x, dd.y, dd.z);
		}
		atoms[i].neigh =
		  (neigh_t *)realloc(atoms[i].neigh,
		  (atoms[i].n_neigh + 1) * sizeof(neigh_t));
		dd.x /= r;
		dd.y /= r;
		dd.z /= r;
		k = atoms[i].n_neigh;
		atoms[i].neigh[k].typ = typ2;
		atoms[i].neigh[k].nr = j;
		atoms[i].neigh[k].r = r;
		atoms[i].neigh[k].dist = dd;
#ifdef COULOMB
		atoms[i].neigh[k].r2 = r * r;
#endif /* COULOMB */
#ifdef ADP
		atoms[i].neigh[k].rdist.x = dd.x * r;
		atoms[i].neigh[k].rdist.y = dd.y * r;
		atoms[i].neigh[k].rdist.z = dd.z * r;
		atoms[i].neigh[k].sqrdist.xx = dd.x * dd.x * r * r;
		atoms[i].neigh[k].sqrdist.yy = dd.y * dd.y * r * r;
		atoms[i].neigh[k].sqrdist.zz = dd.z * dd.z * r * r;
		atoms[i].neigh[k].sqrdist.yz = dd.y * dd.z * r * r;
		atoms[i].neigh[k].sqrdist.zx = dd.z * dd.x * r * r;
		atoms[i].neigh[k].sqrdist.xy = dd.x * dd.y * r * r;
#endif /* ADP */
		atoms[i].n_neigh++;

		col =
		  (typ1 <=
		  typ2) ? typ1 * ntypes + typ2 - ((typ1 * (typ1 + 1)) / 2)
		  : typ2 * ntypes + typ1 - ((typ2 * (typ2 + 1)) / 2);
		atoms[i].neigh[k].col[0] = col;
		mindist[col] = MIN(mindist[col], r);

		/* pre-compute index and shift into potential table */
		/* pair potential */
		if (!sh_dist) {
		  if (format == 0 || format == 3) {
		    rr = r - calc_pot.begin[col];
		    if (rr < 0) {
		      fprintf(stderr,
			"The distance %f is smaller than the beginning\n", r);
		      fprintf(stderr, "of the potential #%d (r_begin=%f).\n",
			col, calc_pot.begin[col]);
		      fflush(stdout);
		      error(1, "Short distance!");
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
#if defined EAM || defined ADP
		  /* transfer function */
		  col = paircol + typ2;
		  atoms[i].neigh[k].col[1] = col;
		  if (format == 0 || format == 3) {
		    rr = r - calc_pot.begin[col];
		    if (rr < 0) {
		      fprintf(stderr,
			"The distance %f is smaller than the beginning\n", r);
		      fprintf(stderr, "of the potential #%d (r_begin=%f).\n",
			col, calc_pot.begin[col]);
		      fflush(stdout);
		      error(1, "short distance in config.c!");
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
#endif /* EAM || ADP */
#ifdef ADP
		  /* dipole part */
		  col = paircol + 2 * ntypes + atoms[i].neigh[k].col[0];
		  atoms[i].neigh[k].col[2] = col;
		  if (format == 0 || format == 3) {
		    rr = r - calc_pot.begin[col];
		    if (rr < 0) {
		      fprintf(stderr,
			"The distance %f is smaller than the beginning\n", r);
		      fprintf(stderr, "of the potential #%d (r_begin=%f).\n",
			col, calc_pot.begin[col]);
		      fflush(stdout);
		      error(1, "short distance in config.c!");
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
		    step = calc_pot.xcoord[khi] - calc_pot.xcoord[klo];
		    shift = (r - calc_pot.xcoord[klo]) / step;

		  }
		  /* Check if we are at the last index */
		  if (slot >= calc_pot.last[col]) {
		    slot--;
		    shift += 1.0;
		  }
		  atoms[i].neigh[k].shift[2] = shift;
		  atoms[i].neigh[k].slot[2] = slot;
		  atoms[i].neigh[k].step[2] = step;

		  /* quadrupole part */
		  col = 2 * paircol + 2 * ntypes + atoms[i].neigh[k].col[0];
		  atoms[i].neigh[k].col[3] = col;
		  if (format == 0 || format == 3) {
		    rr = r - calc_pot.begin[col];
		    if (rr < 0) {
		      fprintf(stderr,
			"The distance %f is smaller than the beginning\n", r);
		      fprintf(stderr, "of the potential #%d (r_begin=%f).\n",
			col, calc_pot.begin[col]);
		      fflush(stdout);
		      error(1, "short distance in config.c!");
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
		    step = calc_pot.xcoord[khi] - calc_pot.xcoord[klo];
		    shift = (r - calc_pot.xcoord[klo]) / step;

		  }
		  /* Check if we are at the last index */
		  if (slot >= calc_pot.last[col]) {
		    slot--;
		    shift += 1.0;
		  }
		  atoms[i].neigh[k].shift[3] = shift;
		  atoms[i].neigh[k].slot[3] = slot;
		  atoms[i].neigh[k].step[3] = step;
#endif /* ADP */
		}
	      }
	    }
      }
      maxneigh = MAX(maxneigh, atoms[i].n_neigh);
      reg_for_free(atoms[i].neigh, "neighbor table atom %d", i);
    }

/* increment natoms and configuration number */
    natoms += count;
    nconf++;

  } while (!feof(infile));
  fclose(infile);

  /* be pedantic about too large ntypes */
  if ((max_type + 1) < ntypes) {
    error(0, "There are less than %d atom types in your configurations!\n",
      ntypes);
    error(1, "Please adjust \"ntypes\" in your parameter file.", ntypes);
  }

  reg_for_free(atoms, "atoms");
  reg_for_free(coheng, "coheng");
  reg_for_free(conf_weight, "conf_weight");
  reg_for_free(volumen, "volumen");
  reg_for_free(stress, "stress");
  reg_for_free(inconf, "inconf");
  reg_for_free(cnfstart, "cnfstart");
  reg_for_free(useforce, "useforce");
  reg_for_free(usestress, "usestress");
#ifdef CONTRIB
  if (n_spheres > 0) {
    reg_for_free(r_spheres, "sphere radii");
    reg_for_free(sphere_centers, "sphere centers");
  }
#endif

  mdim = 3 * natoms + 7 * nconf;	/* mdim is dimension of force vector
					   3*natoms are double forces,
					   nconf cohesive energies,
					   6*nconf stress tensor components */
#if defined EAM || defined ADP
  mdim += nconf;		/* nconf limiting constraints */
  mdim += 2 * ntypes;		/* ntypes dummy constraints */
#endif /* EAM ADP */
#ifdef APOT
  mdim += opt_pot.idxlen;	/* 1 slot for each analytic parameter -> punishment */
  mdim += apot_table.number + 1;	/* 1 slot for each analytic potential -> punishment */
#endif /* APOT */

  /* copy forces into single vector */
  if (NULL == (force_0 = (double *)malloc(mdim * sizeof(double))))
    error(1, "Cannot allocate force vector");
  reg_for_free(force_0, "force_0");

  k = 0;
  for (i = 0; i < natoms; i++) {	/* first forces */
    force_0[k++] = atoms[i].force.x;
    force_0[k++] = atoms[i].force.y;
    force_0[k++] = atoms[i].force.z;
  }
  for (i = 0; i < nconf; i++) {	/* then cohesive energies */
    force_0[k++] = coheng[i];
  }
#ifdef STRESS
  for (i = 0; i < nconf; i++) {	/* then stresses */
    if (usestress[i]) {
      force_0[k++] = stress[i].xx;
      force_0[k++] = stress[i].yy;
      force_0[k++] = stress[i].zz;
      force_0[k++] = stress[i].xy;
      force_0[k++] = stress[i].yz;
      force_0[k++] = stress[i].zx;
    } else {
      for (j = 0; j < 6; j++)
	force_0[k++] = 0.;
    }
  }
#else
  for (i = 0; i < 6 * nconf; i++)
    force_0[k++] = 0.;
#endif /* STRESS */
#if defined EAM || defined ADP
  for (i = 0; i < nconf; i++)
    force_0[k++] = 0.;		/* punishment rho out of bounds */
  for (i = 0; i < 2 * ntypes; i++) {	/* constraint on U(n=0):=0 */
    /* XXX and U'(n_mean)=0  */
    force_0[k++] = 0.;
  }
#endif /* EAM || ADP */

  /* write pair distribution file */
  if (write_pair == 1) {
    char  pairname[255];
    FILE *pairfile;
#ifdef APOT
    int   pair_steps = APOT_STEPS / 2;
#else
    int   pair_steps = 1000 / 2;
#endif /* APOT */
    double pair_table[paircol * pair_steps];
    double pair_dist[paircol];
    int   pos, max_count = 0;

    strcpy(pairname, config);
    strcat(pairname, ".pair");
    pairfile = fopen(pairname, "w");
    fprintf(pairfile, "# radial distribution file for %d potential(s)\n",
      paircol);

    for (i = 0; i < paircol * pair_steps; i++)
      pair_table[i] = 0.;

    for (i = 0; i < ntypes; i++)
      for (k = 0; k < ntypes; k++)
	pair_dist[(i <=
	    k) ? i * ntypes + k - (i * (i + 1) / 2) : k * ntypes + i - (k * (k +
	      1) / 2)] = rcut[i * ntypes + k] / pair_steps;

    for (k = 0; k < paircol; k++) {
      for (i = 0; i < natoms; i++) {
	typ1 = atoms[i].typ;
	for (j = 0; j < atoms[i].n_neigh; j++) {
	  typ2 = atoms[i].neigh[j].typ;
	  col =
	    (typ1 <=
	    typ2) ? typ1 * ntypes + typ2 - ((typ1 * (typ1 +
		1)) / 2) : typ2 * ntypes + typ1 - ((typ2 * (typ2 + 1)) / 2);
	  if (col == k) {
	    pos = (int)(atoms[i].neigh[j].r / pair_dist[k]);
#ifdef DEBUG
	    if (atoms[i].neigh[j].r <= 1) {
	      fprintf(stderr, "Short distance (%f) found.\n",
		atoms[i].neigh[j].r);
	      fprintf(stderr, "\tatom=%d neighbor=%d\n", i, j);
	    }
#endif /* DEBUG */
	    pair_table[k * pair_steps + pos]++;
	    if ((int)pair_table[k * pair_steps + pos] > max_count)
	      max_count = (int)pair_table[k * pair_steps + pos];
	  }
	}
      }
    }

    for (k = 0; k < paircol; k++) {
      for (i = 0; i < pair_steps; i++) {
	pair_table[k * pair_steps + i] /= max_count;
	fprintf(pairfile, "%f %f\n", i * pair_dist[k],
	  pair_table[k * pair_steps + i]);
      }
      if (k != (paircol - 1))
	fprintf(pairfile, "\n\n");
    }
    fclose(pairfile);
  }

  /* assign correct distances to different tables */
#ifdef APOT
  double min = 10.;

  for (i = 0; i < ntypes; i++)
    for (j = 0; j < ntypes; j++) {
      k =
	(i <=
	j) ? i * ntypes + j - ((i * (i + 1)) / 2) : j * ntypes + i - ((j * (j +
	    1)) / 2);
      if (mindist[k] == 99)
	mindist[k] = 3;
      rmin[i * ntypes + j] = mindist[k];
      apot_table.begin[k] = mindist[k] * 0.95;
      opt_pot.begin[k] = mindist[k] * 0.95;
      calc_pot.begin[k] = mindist[k] * 0.95;
      min = MIN(min, mindist[k]);
    }
#if defined EAM || defined ADP
  for (i = 0; i < ntypes; i++) {
    j = i + ntypes * (ntypes + 1) / 2;
    apot_table.begin[j] = min * 0.95;
    opt_pot.begin[j] = min * 0.95;
    calc_pot.begin[j] = min * 0.95;
  }
#endif /* EAM || ADP */
#ifdef ADP
  for (i = 0; i < paircol; i++) {
    apot_table.begin[paircol + 2 * ntypes + i] = min * 0.95;
    apot_table.begin[2 * paircol + 2 * ntypes + i] = min * 0.95;
    opt_pot.begin[paircol + 2 * ntypes + i] = min * 0.95;
    opt_pot.begin[2 * paircol + 2 * ntypes + i] = min * 0.95;
    calc_pot.begin[paircol + 2 * ntypes + i] = min * 0.95;
    calc_pot.begin[2 * paircol + 2 * ntypes + i] = min * 0.95;
  }
#endif /* ADP */
  /* recalculate step, invstep and xcoord for new tables */
  for (i = 0; i < calc_pot.ncols; i++) {
    calc_pot.step[i] = (calc_pot.end[i] - calc_pot.begin[i]) / (APOT_STEPS - 1);
    calc_pot.invstep[i] = 1. / calc_pot.step[i];
    for (j = 0; j < APOT_STEPS; j++) {
      index = i * APOT_STEPS + (i + 1) * 2 + j;
      calc_pot.xcoord[index] = calc_pot.begin[i] + j * calc_pot.step[i];
    }
  }

  update_slots();
#endif /* APOT */

  /* print minimal distance matrix */
  printf("\nMinimal Distances Matrix:\n");
  printf("Atom\t");
  for (i = 0; i < ntypes; i++)
    printf("%8s\t", elements[i]);
  printf("with\n");
  for (i = 0; i < ntypes; i++) {
    printf("%s\t", elements[i]);
    for (j = 0; j < ntypes; j++) {
      k =
	(i <=
	j) ? i * ntypes + j - ((i * (i + 1)) / 2) : j * ntypes + i - ((j * (j +
	    1)) / 2);
      printf("%f\t", mindist[k]);

    }
    printf("\n");
  }
  printf("\n");

  free(mindist);

  /* calculate the total number of the atom types */
  na_type = (int **)realloc(na_type, (nconf + 1) * sizeof(int *));
  reg_for_free(na_type, "na_type");
  if (NULL == na_type)
    error(1, "Cannot allocate memory for na_type");
  na_type[nconf] = (int *)malloc(ntypes * sizeof(int));
  reg_for_free(na_type[nconf], "na_type[%d]", nconf);
  for (i = 0; i < ntypes; i++)
    na_type[nconf][i] = 0;

  for (i = 0; i < nconf; i++)
    for (j = 0; j < ntypes; j++)
      na_type[nconf][j] += na_type[i][j];

  /* print diagnostic message and close file */
  printf("Read %d configurations (%d with forces, %d with stresses)\n", nconf,
    w_force, w_stress);
  printf("with a total of %d atoms (", natoms);
  for (i = 0; i < ntypes; i++) {
    if (have_elements)
      printf("%d %s (%.2f%%)", na_type[nconf][i], elements[i],
	100. * na_type[nconf][i] / natoms);
    else
      printf("%d type %d (%.2f%%)", na_type[nconf][i], i,
	100. * na_type[nconf][i] / natoms);
    if (i != (ntypes - 1))
      printf(", ");
  }

  printf(")\nfrom file \"%s\".\n\n", filename);
  if (sh_dist)
    error(1,
      "Distances too short, last occurence conf %d, see above for details\n",
      sh_dist);
  return;
}

/****************************************************************
 *
 *  compute box transformation matrix
 *
 ****************************************************************/

double make_box(void)
{
  double volume;

  /* compute tbox_j such that SPROD(box_i,tbox_j) == delta_ij */
  /* first unnormalized */
  tbox_x = vec_prod(box_y, box_z);
  tbox_y = vec_prod(box_z, box_x);
  tbox_z = vec_prod(box_x, box_y);

  /* volume */
  volume = SPROD(box_x, tbox_x);
  if (0 == volume)
    error(1, "Box edges are parallel\n");

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

#ifdef CONTRIB

/****************************************************************
 *
 *  check if the atom does contribute to the error sum
 *
 ****************************************************************/

int does_contribute(vector pos)
{
  int   i;
  double n_a, n_b, n_c, r;
  vector dist;

  if (have_contrib_box) {
    dist.x = pos.x - cbox_o.x;
    dist.y = pos.y - cbox_o.y;
    dist.z = pos.z - cbox_o.z;
    n_a = SPROD(dist, cbox_a);
    n_b = SPROD(dist, cbox_b);
    n_c = SPROD(dist, cbox_c);
    if (n_a >= 0 && n_a <= 1)
      if (n_b >= 0 && n_b <= 1)
	if (n_c >= 0 && n_c <= 1)
	  return 1;
  }

  for (i = 0; i < n_spheres; i++) {
    dist.x = (pos.x - sphere_centers[i].x);
    dist.y = (pos.y - sphere_centers[i].y);
    dist.z = (pos.z - sphere_centers[i].z);
    r = SPROD(dist, dist);
    r = sqrt(r);
    if (r < r_spheres[i])
      return 1;
  }

  return 0;
}

#endif /* CONTRIB */

#ifdef APOT

/****************************************************************
 *
 * recalculate the slots of the atoms for analytic potential
 *
 ****************************************************************/

void update_slots(void)
{
  int   i, j;
  int   col0;			/* pair potential part */
#if defined EAM || defined ADP
  int   col1;			/* transfer function part */
#endif /* EAM || ADP */
#ifdef ADP
  int   col2, col3;		/* u and w function part */
#endif /* ADP */
  double r, rr;

  for (i = 0; i < natoms; i++) {
    for (j = 0; j < atoms[i].n_neigh; j++) {
      r = atoms[i].neigh[j].r;

      /* update slots for pair potential part, slot 0 */
      col0 = atoms[i].neigh[j].col[0];
      if (r < calc_pot.end[col0]) {
	rr = r - calc_pot.begin[col0];
	atoms[i].neigh[j].slot[0] = (int)(rr * calc_pot.invstep[col0]);
	atoms[i].neigh[j].step[0] = calc_pot.step[col0];
	atoms[i].neigh[j].shift[0] =
	  (rr -
	  atoms[i].neigh[j].slot[0] * calc_pot.step[col0]) *
	  calc_pot.invstep[col0];
	/* move slot to the right potential */
	atoms[i].neigh[j].slot[0] += calc_pot.first[col0];
      }
#if defined EAM || defined ADP
      /* update slots for eam transfer functions, slot 1 */
      col1 = atoms[i].neigh[j].col[1];
      if (r < calc_pot.end[col0]) {
	rr = r - calc_pot.begin[col1];
	atoms[i].neigh[j].slot[1] = (int)(rr * calc_pot.invstep[col1]);
	atoms[i].neigh[j].step[1] = calc_pot.step[col1];
	atoms[i].neigh[j].shift[1] =
	  (rr -
	  atoms[i].neigh[j].slot[1] * calc_pot.step[col1]) *
	  calc_pot.invstep[col1];
	/* move slot to the right potential */
	atoms[i].neigh[j].slot[1] += calc_pot.first[col1];
      }
#endif /* EAM || ADP */
#ifdef ADP
      /* update slots for adp dipole functions, slot 2 */
      col2 = atoms[i].neigh[j].col[2];
      if (r < calc_pot.end[col2]) {
	rr = r - calc_pot.begin[col2];
	atoms[i].neigh[j].slot[2] = (int)(rr * calc_pot.invstep[col2]);
	atoms[i].neigh[j].step[2] = calc_pot.step[col2];
	atoms[i].neigh[j].shift[2] =
	  (rr -
	  atoms[i].neigh[j].slot[2] * calc_pot.step[col2]) *
	  calc_pot.invstep[col2];
	/* move slot to the right potential */
	atoms[i].neigh[j].slot[2] += calc_pot.first[col2];
      }

      /* update slots for adp quadrupole functions, slot 3 */
      col3 = atoms[i].neigh[j].col[3];
      if (r < calc_pot.end[col3]) {
	rr = r - calc_pot.begin[col3];
	atoms[i].neigh[j].slot[3] = (int)(rr * calc_pot.invstep[col3]);
	atoms[i].neigh[j].step[3] = calc_pot.step[col3];
	atoms[i].neigh[j].shift[3] =
	  (rr -
	  atoms[i].neigh[j].slot[3] * calc_pot.step[col3]) *
	  calc_pot.invstep[col3];
	/* move slot to the right potential */
	atoms[i].neigh[j].slot[3] += calc_pot.first[col3];
      }
#endif /* ADP */
    }
  }
}

#endif /* APOT */
