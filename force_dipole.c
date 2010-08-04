/****************************************************************
*
* force.c: Routines used for calculating pair/monopole/dipole
*     forces/energies in various interpolation schemes.
*
*****************************************************************/
/*
*   Copyright 2002-2010 Peter Brommer, Franz G"ahler, Daniel Schopf, 
*                       Philipp Beck
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
* $Revision: 1.2 $
* $Date: 2010/04/22 13:29:40 $
*****************************************************************/

#ifdef DIPOLE
#include "potfit.h"

/*****************************************************************************
*
*  compute forces using pair potentials with spline interpolation
*
*  returns sum of squares of differences between calculated and reference
*     values
*
*  arguments: *xi - pointer to short-range potential
*             *xi_c, *xi_cd and *xi_d  - pointer to electrostatic potentials
*             *forces - pointer to forces calculated from potential
*             flag - used for special tasks
*
* When using the mpi-parallelized version of potfit, all processes but the
* root process jump into this function immediately after initialization and
* stay in here for an infinite loop, to exit only when a certain flag value
* is passed from process 0. When a set of forces needs to be calculated,
* the root process enters the function with a flag value of 0, broadcasts
* the current potential table xi and the flag value to the other processes,
* thus initiating a force calculation. Whereas the root process returns with
* the result, the other processes stay in the loop. If the root process is
* called with flag value 1, all processes exit the function without
* calculating the forces.
* If anything changes about the potential beyond the values of the parameters,
* e.g. the location of the sampling points, these changes have to be broadcast
* from rank 0 process to the higher ranked processes. This is done when the
* root process is called with flag value 2. Then a potsync function call is
* initiated by all processes to get the new potential from root.
*
* xi_opt is the array storing the potential parameters (usually it is the
*     opt_pot.table - part of the struct opt_pot, but it can also be
*     modified from the current potential.
*
* forces is the array storing the deviations from the reference data, not
*     only for forces, but also for energies, stresses or dummy constraints
*     (if applicable).
*
* flag is an integer controlling the behaviour of calc_forces_pair.
*    flag == 1 will cause all processes to exit calc_forces_pair after
*             calculation of forces.
*    flag == 2 will cause all processes to perform a potsync (i.e. broadcast
*             any changed potential parameters from process 0 to the others)
*             before calculation of forces
*    all other values will cause a set of forces to be calculated. The root
*             process will return with the sum of squares of the forces,
*             while all other processes remain in the function, waiting for
*             the next communication initiating another force calculation
*             loop
*
******************************************************************************/

real calc_forces_dipole(real *xi_opt, real *forces, int flag)
{
  real  tmpsum, sum = 0.;
  int   first, col, ne, size, i;
  real *xi = NULL;
  real *xi_c = NULL;
  real *xi_cd = NULL;
  real *xi_d = NULL;
  apot_table_t *apt = &apot_table;
  FILE *outfile2;
  char *filename2 = "Dipol_Konvergenz_Verlauf";
  int sum_c = 0;
  int sum_t = 0;

  switch (format) {
      case 0:
	xi = calc_pot.table;
	break;
      case 3:			/* fall through */
      case 4:
	xi = xi_opt;		/* calc-table is opt-table */
	break;
      case 5:
	xi = calc_pot.table;	/* we need to update the calc-table */
  }

  xi_c = calc_pot.table_c;
  xi_cd = calc_pot.table_cd;
  xi_d = calc_pot.table_d;
  ne =  apot_table.total_ne_par;
  size = apt->number;

  /* This is the start of an infinite loop */
  while (1) {
    tmpsum = 0.;		/* sum of squares of local process */
#ifndef APOT
    if (format > 4 && myid == 0)
      update_calc_table(xi_opt, xi, 0);
#endif /* APOT */

#if defined APOT && !defined MPI
    if (format == 0) {
      apot_check_params(xi_opt);
      update_calc_table(xi_opt, xi, 0);
    }
#endif

#ifdef MPI
    /* exchange potential and flag value */
#ifndef APOT
    MPI_Bcast(xi, calc_pot.len, REAL, 0, MPI_COMM_WORLD);
#endif /* APOT */
    MPI_Bcast(&flag, 1, MPI_INT, 0, MPI_COMM_WORLD);

#ifdef APOT
    if (myid == 0)
      apot_check_params(xi_opt);
    MPI_Bcast(xi_opt, ndimtot, REAL, 0, MPI_COMM_WORLD);
    if (format == 0)
      update_calc_table(xi_opt, xi, 0);
    if (flag == 1)
      break;
#else /* APOT */

    /* if flag==2 then the potential parameters have changed -> sync */
    if (flag == 2)
      potsync();
    /* non root processes hang on, unless...  */
    if (flag == 1)
      break;			/* Exception: flag 1 means clean up */
#endif /* APOT */
#endif /* MPI */

      /* init second derivatives for splines */
      for (col = 0; col < paircol; col++) {
	first = calc_pot.first[col];
	if (format == 3 || format == 0) {
	  spline_ed(calc_pot.step[col], xi + first,
		    calc_pot.last[col] - first + 1,
		    *(xi + first - 2), 0.0, calc_pot.d2tab + first);
	} else {     	                       	/* format >= 4 ! */
	  spline_ne(calc_pot.xcoord + first, xi + first,
		    calc_pot.last[col] - first + 1,
		    *(xi + first - 2), 0.0, calc_pot.d2tab + first);
	}
      }
      if(!cs) {
	for (col = 0; col < paircol; col++) {
	  first = calc_pot.first[col];
	  spline_ed(calc_pot.step[col], xi_c + first,
		    calc_pot.last[col] - first + 1,
		    *(xi_c + first - 2), 0.0, calc_pot.d2tab_c + first);
	  spline_ed(calc_pot.step[col], xi_cd + first,
		    calc_pot.last[col] - first + 1,
		    *(xi_cd + first - 2), 0.0, calc_pot.d2tab_cd + first);
	  spline_ed(calc_pot.step[col], xi_d + first,
		    calc_pot.last[col] - first + 1,
		    *(xi_d + first - 2), 0.0, calc_pot.d2tab_d + first);
	}
	cs++;
      }

      /* write potential and d2 tables:
       for(i=0;i<200;i++)
	 printf("%d\t%lf\n", i, xi_c[i]);
       for(i=0;i<200;i++)
	 printf("%d\t%lf\n", i + 200, xi_cd[i]);
       for(i=0;i<200;i++)
	 printf("%d\t%lf\n", i + 400, xi_d[i]);
       for(i=0;i<200;i++)
	 printf("%d\t%lf\n", i + 600, calc_pot.d2tab_c[i]);
       for(i=0;i<200;i++)
	 printf("%d\t%lf\n", i + 800, calc_pot.d2tab_cd[i]);
       for(i=0;i<200;i++)
	 printf("%d\t%lf\n", i + 1000, calc_pot.d2tab_d[i]);
	 error(""); */
       

#ifndef MPI
    myconf = nconf;
#endif

#ifdef _OPENMP
#pragma omp parallel
#endif

    /* region containing loop over configurations,
       also OMP-parallelized region */
    {
      int   self;
      vector tmp_force;
      int   h, j, k, l, typ1, typ2, uf, us, stresses;	// config
      real  fnval, grad, fnval_tail, grad_tail, grad_i, grad_j, p_sr_tail;
      atom_t *atom;
      neigh_t *neigh;

#ifdef _OPENMP
#pragma omp for reduction(+:tmpsum,rho_sum_loc)
#endif
      
   
      for (i = 0; i < natoms; i++) {
	/* reset dipoles and fields: LOOP Z E R O */
	atoms[i].E_stat.x = 0;
	atoms[i].E_stat.y = 0;
	atoms[i].E_stat.z = 0;
	atoms[i].p_sr.x = 0;
	atoms[i].p_sr.y = 0;
	atoms[i].p_sr.z = 0;
	atoms[i].E_ind.x = 0;
	atoms[i].E_ind.y = 0;
	atoms[i].E_ind.z = 0;
	atoms[i].p_ind.x = 0;
	atoms[i].p_ind.y = 0;
	atoms[i].p_ind.z = 0;
	atoms[i].E_old.x = 0;
	atoms[i].E_old.y = 0;
	atoms[i].E_old.z = 0;
	atoms[i].E_temp.x = 0;
	atoms[i].E_temp.y = 0;
	atoms[i].E_temp.z = 0;
	/* initialize neighbour-counter for potential calculation */
	if(cs == 1) {
	  for (j = 0; j < atoms[i].n_neigh; j++) {
	    atoms[i].neigh[j].fnval_c = 0.;
	    atoms[i].neigh[j].grad_c = 0.;
	    atoms[i].neigh[j].fnval_cd = 0.;
	    atoms[i].neigh[j].grad_cd = 0.;
	    atoms[i].neigh[j].fnval_d = 0.;
	    atoms[i].neigh[j].grad_d = 0.;
	  }
	}
      }
      cs++;

      /* loop over configurations: M A I N LOOP CONTAINING ALL ATOM-LOOPS */
      for (h = firstconf; h < firstconf + myconf; h++) {
	uf = conf_uf[h - firstconf];
	us = conf_us[h - firstconf];
	/* reset energies and stresses */
	forces[energy_p + h] = 0.;
	for (i = 0; i < 6; i++)
	  forces[stress_p + 6 * h + i] = 0.;

	/* F I R S T LOOP OVER ATOMS: reset forces, dipoles */
	for (i = 0; i < inconf[h]; i++) {    //atoms
	  if (uf) {
	    k = 3 * (cnfstart[h] + i);
	    forces[k] = -force_0[k];
	    forces[k + 1] = -force_0[k + 1];
	    forces[k + 2] = -force_0[k + 2];
	  } else {
	    k = 3 * (cnfstart[h] + i);
	    forces[k] = 0.;
	    forces[k + 1] = 0.;
	    forces[k + 2] = 0.;
	  }
	}	/* end F I R S T LOOP *

	/* S E C O N D loop: calculate short-range and monopole forces,
	   calculate static field- and dipole-contributions */
	for (i = 0; i < inconf[h]; i++) {       //atoms
	  atom = conf_atoms + i + cnfstart[h] - firstatom;
	  typ1 = atom->typ;
	  k = 3 * (cnfstart[h] + i);
	  for (j = 0; j < atom->n_neigh; j++) {    //neighbors
	    neigh = atom->neigh + j;
	    typ2 = neigh->typ;
	    col = neigh->col[0];

	    /* In small cells, an atom might interact with itself */
	    self = (neigh->nr == i + cnfstart[h]) ? 1 : 0;

	      /* calculate short-range forces */
	      if (neigh->r < calc_pot.end[col]) {

	
		  if (uf) {
		    fnval =
		      splint_comb_dir(&calc_pot, xi, neigh->slot[0],
				      neigh->shift[0], neigh->step[0], &grad);
		  } else {
		    fnval =
		      splint_dir(&calc_pot, xi, neigh->slot[0], neigh->shift[0],
				 neigh->step[0]);
		  }
		
		/* avoid double counting if atom is interacting with a
		   copy of itself */
		if (self) {
		  fnval *= 0.5;
		  grad *= 0.5;
		}
		forces[energy_p + h] += fnval;

		if (uf) {
		  tmp_force.x = neigh->dist.x * grad;
		  tmp_force.y = neigh->dist.y * grad;
		  tmp_force.z = neigh->dist.z * grad;
		  forces[k] += tmp_force.x;
		  forces[k + 1] += tmp_force.y;
		  forces[k + 2] += tmp_force.z;
		  /* actio = reactio */
		  l = 3 * neigh->nr;
		  forces[l] -= tmp_force.x;
		  forces[l + 1] -= tmp_force.y;
		  forces[l + 2] -= tmp_force.z;

#ifdef STRESS
		  /* calculate pair stresses */
		  if (us) {
		    tmp_force.x *= neigh->r;
		    tmp_force.y *= neigh->r;
		    tmp_force.z *= neigh->r;
		    stresses = stress_p + 6 * h;
		    forces[stresses] -= neigh->dist.x * tmp_force.x;
		    forces[stresses + 1] -= neigh->dist.y * tmp_force.y;
		    forces[stresses + 2] -= neigh->dist.z * tmp_force.z;
		    forces[stresses + 3] -= neigh->dist.x * tmp_force.y;
		    forces[stresses + 4] -= neigh->dist.y * tmp_force.z;
		    forces[stresses + 5] -= neigh->dist.z * tmp_force.x;
		  }
#endif /* STRESS */
		}
	      }

	      /* calculate monopole forces */
	      if (neigh->r < dp_cut && (xi_opt[2*size + ne + typ1] || xi_opt[2*size + ne + typ2])) { 
		
		if (cs == 2) {
		  neigh->fnval_c = splint_comb_dir_c(&calc_pot, xi_c, 
						     neigh->slot[0],
						     neigh->shift[0],
						     neigh->step[0], &neigh->grad_c);
		}
		fnval_tail = neigh->fnval_c;
		grad_tail = neigh->grad_c;

		grad_i = xi_opt[2*size + ne + typ2] * grad_tail;
		if(typ1 == typ2) {
		  grad_j = grad_i;
		} else {
		  grad_j = xi_opt[2*size + ne + typ1] * grad_tail;
		}
		fnval =  xi_opt[2*size + ne + typ1] * xi_opt[2*size + ne + typ2] * fnval_tail;
		grad =  xi_opt[2*size + ne + typ1] * grad_i;
		
		if (self) {
		  grad_i *= 0.5;
		  grad_j *= 0.5;  
		  fnval *= 0.5;
		  grad *= 0.5;
		}
		
		forces[energy_p + h] += fnval;

		if (uf) {
		  tmp_force.x = neigh->dist.x * grad;
		  tmp_force.y = neigh->dist.y * grad;
		  tmp_force.z = neigh->dist.z * grad;
		  forces[k] += tmp_force.x;
		  forces[k + 1] += tmp_force.y;
		  forces[k + 2] += tmp_force.z;
		  /* actio = reactio */
		  l = 3 * neigh->nr;
		  forces[l] -= tmp_force.x;
		  forces[l + 1] -= tmp_force.y;
		  forces[l + 2] -= tmp_force.z;

#ifdef STRESS
		  /* calculate stresses */
		  if (us) {
		    tmp_force.x *= neigh->r;
		    tmp_force.y *= neigh->r;
		    tmp_force.z *= neigh->r;
		    stresses = stress_p + 6 * h;
		    forces[stresses] -= neigh->dist.x * tmp_force.x;
		    forces[stresses + 1] -= neigh->dist.y * tmp_force.y;
		    forces[stresses + 2] -= neigh->dist.z * tmp_force.z;
		    forces[stresses + 3] -= neigh->dist.x * tmp_force.y;
		    forces[stresses + 4] -= neigh->dist.y * tmp_force.z;
		    forces[stresses + 5] -= neigh->dist.z * tmp_force.x;
		  }
#endif /* STRESS */
		}

		/* calculate static field-contributions */
		atom->E_stat.x += neigh->dist.x * grad_i;
		atom->E_stat.y += neigh->dist.y * grad_i;
		atom->E_stat.z += neigh->dist.z * grad_i; 
		atoms[neigh->nr].E_stat.x += neigh->dist.x * grad_j;  
		atoms[neigh->nr].E_stat.y += neigh->dist.y * grad_j;  
		atoms[neigh->nr].E_stat.z += neigh->dist.z * grad_j;  

		/* calculate short-range dipoles */
		if( (xi_opt[2*size + ne + ntypes + typ1]) && (xi_opt[2*size + ne + 2*ntypes + col]) && (xi_opt[3*size + ne + 2*ntypes + col]) ) {	
		  p_sr_tail = shortrange_value(neigh->r, &xi_opt[2*size + ne + ntypes + typ1], &xi_opt[2*size + ne + 2*ntypes + col], 
					       &xi_opt[3*size + ne + 2*ntypes + col]);					     
		atom->p_sr.x += xi_opt[2*size + ne + typ2] * neigh->dist.x * p_sr_tail;
		atom->p_sr.y += xi_opt[2*size + ne + typ2] * neigh->dist.y * p_sr_tail;
		atom->p_sr.z += xi_opt[2*size + ne + typ2] * neigh->dist.z * p_sr_tail;
		}
		if( (xi_opt[2*size + ne + ntypes + typ2]) && (xi_opt[2*size + ne + 2*ntypes + col]) && (xi_opt[3*size + ne + 2*ntypes + col]) && !self) {
		  p_sr_tail = shortrange_value(neigh->r, &xi_opt[2*size + ne + ntypes + typ2], &xi_opt[2*size + ne + 2*ntypes + col], 
					       &xi_opt[3*size + ne + 2*ntypes + col]);
		  atoms[neigh->nr].p_sr.x += xi_opt[2*size + ne + typ1] * neigh->dist.x * p_sr_tail;
		  atoms[neigh->nr].p_sr.y += xi_opt[2*size + ne + typ1] * neigh->dist.y * p_sr_tail;
		  atoms[neigh->nr].p_sr.z += xi_opt[2*size + ne + typ1] * neigh->dist.z * p_sr_tail;
		}
		
		}	      
	  }			/* loop over neighbours */

	  /*then we can calculate contribution of forces right away */
	  if (uf) {
#ifdef FWEIGHT
	    /* Weigh by absolute value of force */
	    forces[k] /= FORCE_EPS + atom->absforce;
	    forces[k + 1] /= FORCE_EPS + atom->absforce;
	    forces[k + 2] /= FORCE_EPS + atom->absforce;
#endif /* FWEIGHT */
	    /* Returned force is difference between */
	    /* calculated and input force */
	    tmpsum +=
	      conf_weight[h] * (SQR(forces[k]) + SQR(forces[k + 1]) +
				SQR(forces[k + 2]));
	  }
	}         /* end S E C O N D loop over atoms */


	/* T H I R D loop: calculate whole dipole moment for every atom */
	real rp, dp_sum;
	int dp_converged = 0, dp_it = 0;
	real max_diff = 10.;

	while(dp_converged == 0) {
	  dp_sum = 0;
	  for (i = 0; i < inconf[h]; i++) {    //atoms	  
	    atom = conf_atoms + i + cnfstart[h] - firstatom;
	    typ1 = atom->typ;
	
	      if(xi_opt[2*size + ne + ntypes + typ1]) {

		atom->p_ind.x = xi_opt[2*size + ne + ntypes + typ1] * (atom->E_stat.x + atom->E_ind.x) + atom->p_sr.x;
		atom->p_ind.y = xi_opt[2*size + ne + ntypes + typ1] * (atom->E_stat.y + atom->E_ind.y) + atom->p_sr.x;
		atom->p_ind.z = xi_opt[2*size + ne + ntypes + typ1] * (atom->E_stat.z + atom->E_ind.z) + atom->p_sr.x;
	      
		atom->E_temp.x = 0;
		atom->E_temp.y = 0;
		atom->E_temp.z = 0;

		atom->E_old.x = atom->E_ind.x;
		atom->E_old.y = atom->E_ind.y;
		atom->E_old.z = atom->E_ind.z;
	      } 
	  }
    
	  
	  for (i = 0; i < inconf[h]; i++) {     //atoms	  
	    atom = conf_atoms + i + cnfstart[h] - firstatom;
	    typ1 = atom->typ;
	    for (j = 0; j < atom->n_neigh; j++) {  //neighbors
	      neigh = atom->neigh + j;
	      typ2 = neigh->typ;
	      col = neigh->col[0];
	      /* In small cells, an atom might interact with itself */
	      self = (neigh->nr == i + cnfstart[h]) ? 1 : 0;
	      
	      if (neigh->r < dp_cut) { 
		if(xi_opt[2*size + ne + ntypes + typ2]) {
		  rp = SPROD(atoms[neigh->nr].p_ind, neigh->dist);	      
		  atom->E_temp.x += dp_eps * ( 3 * rp * neigh->dist.x  - atoms[neigh->nr].p_ind.x ) / neigh->r3;
		  atom->E_temp.y += dp_eps * ( 3 * rp * neigh->dist.y  - atoms[neigh->nr].p_ind.y ) / neigh->r3;
		  atom->E_temp.z += dp_eps * ( 3 * rp * neigh->dist.z  - atoms[neigh->nr].p_ind.z ) / neigh->r3;
		}
		if(xi_opt[2*size + ne + ntypes + typ1] && !self) {
		  rp = SPROD(atom->p_ind, neigh->dist);
		  atoms[neigh->nr].E_temp.x += dp_eps * ( 3 * rp * neigh->dist.x - atom->p_ind.x ) / neigh->r3;
		  atoms[neigh->nr].E_temp.y += dp_eps * ( 3 * rp * neigh->dist.y - atom->p_ind.y ) / neigh->r3;
		  atoms[neigh->nr].E_temp.z += dp_eps * ( 3 * rp * neigh->dist.z - atom->p_ind.z ) / neigh->r3;
		}
	      }
	    }
	  }

	  for (i = 0; i < inconf[h]; i++) {    //atoms	  
	    atom = conf_atoms + i + cnfstart[h] - firstatom;
	    typ1 = atom->typ;

	    if(xi_opt[2*size + ne + ntypes + typ1]) {

	      atom->E_ind.x = (1 - dp_mix) * atom->E_temp.x + dp_mix * atom->E_old.x;
	      atom->E_ind.y = (1 - dp_mix) * atom->E_temp.y + dp_mix * atom->E_old.y;
	      atom->E_ind.z = (1 - dp_mix) * atom->E_temp.z + dp_mix * atom->E_old.z;
	     
	      dp_sum += SQR(atom->E_old.x - atom->E_ind.x);
	      dp_sum += SQR(atom->E_old.y - atom->E_ind.y);
	      dp_sum += SQR(atom->E_old.z - atom->E_ind.z);
	    }
	  }
	  
	  dp_sum = sqrt(dp_sum);
	  dp_sum /= inconf[h];

	  if (dp_it) {
	    if( (dp_sum > max_diff) || (dp_it > 50) ) {
	      //if(dp_sum > max_diff)
	      //printf("Too large error (%lf) in dipole iteration loop %d\n", dp_sum, dp_it);
	      //if(dp_it > 50)
	      //printf("Convergence error: dp_sum = %lf after 50 loops\n", dp_sum);
	      sum_c += 50;
	      dp_converged = 1;
	      for (i = 0; i < inconf[h]; i++) {    //atoms	  
		atom = conf_atoms + i + cnfstart[h] - firstatom;
		typ1 = atom->typ;
		if(xi_opt[2*size + ne + ntypes + typ1]) {
		  atom->p_ind.x = xi_opt[2*size + ne + ntypes + typ1] * atom->E_stat.x + atom->p_sr.x;
		  atom->p_ind.y = xi_opt[2*size + ne + ntypes + typ1] * atom->E_stat.y + atom->p_sr.x;
		  atom->p_ind.z = xi_opt[2*size + ne + ntypes + typ1] * atom->E_stat.z + atom->p_sr.x;
		  atom->E_ind.x = atom->E_stat.x;
		  atom->E_ind.y = atom->E_stat.y;
		  atom->E_ind.z = atom->E_stat.z;
		}
	      }
	    } 	    
	  } 

	  if (dp_sum < dp_tol) {
	    dp_converged = 1;
	    sum_c += dp_it;
	    //printf("Converged after %d loops\n", dp_it);
	  }
	  
	  dp_it++;
	}        	/* end T H I R D loop over atoms */  


	/* F O U R T H  loop: calculate monopole-dipole and dipole-dipole forces */   
	real rp_i, rp_j, pp_ij, tmp;  
	real grad_1, grad_2, grad_3;
	for (i = 0; i < inconf[h]; i++) {    //atoms	  
	  atom = conf_atoms + i + cnfstart[h] - firstatom;
	  typ1 = atom->typ;
	  for (j = 0; j < atom->n_neigh; j++) {  //neighbors
	    neigh = atom->neigh + j;
	    typ2 = neigh->typ;
	    col = neigh->col[0];
	    /* In small cells, an atom might interact with itself */
	    self = (neigh->nr == i + cnfstart[h]) ? 1 : 0;
	    if (neigh->r < dp_cut && (xi_opt[2*size + ne + ntypes + typ1] || xi_opt[2*size + ne + ntypes + typ2]) ) { 

	      if(cs == 2) {
		neigh->fnval_cd = splint_comb_dir_cd(&calc_pot, xi_cd, 
						neigh->slot[0],
						neigh->shift[0],
						neigh->step[0], &neigh->grad_cd);
	      }
	      fnval_tail = neigh->fnval_cd;
	      grad_tail = neigh->grad_cd;

	  /* monopole-dipole contributions */
	  if(xi_opt[2*size + ne + typ1] && xi_opt[2*size + ne + ntypes + typ2]) {
	    rp_j = SPROD(atoms[neigh->nr].p_ind, neigh->dist);	      	
	    if (self) {  
	      fnval_tail *= 0.5;
	      grad_tail *= 0.5;
	    }

	    fnval = xi_opt[2*size + ne + typ1] * rp * fnval_tail;
	    grad_1 = xi_opt[2*size + ne + typ1] * rp * grad_tail;
	    grad_2 = xi_opt[2*size + ne + typ1] * fnval_tail;

	    forces[energy_p + h] += fnval;
	    
	    if (uf) {
	      tmp_force.x = neigh->dist.x * grad_1 + atoms[neigh->nr].p_ind.x * grad_2;
	      tmp_force.y = neigh->dist.y * grad_1 + atoms[neigh->nr].p_ind.y * grad_2;
	      tmp_force.z = neigh->dist.z * grad_1 + atoms[neigh->nr].p_ind.z * grad_2;
	      forces[k] += tmp_force.x;
	      forces[k + 1] += tmp_force.y;
	      forces[k + 2] += tmp_force.z;
	      /* actio = reactio */
	      l = 3 * neigh->nr;
	      forces[l] -= tmp_force.x;
	      forces[l + 1] -= tmp_force.y;
	      forces[l + 2] -= tmp_force.z;
	      
#ifdef STRESS
	      /* calculate stresses */
	      if (us) {
		tmp_force.x *= neigh->r;
		tmp_force.y *= neigh->r;
		tmp_force.z *= neigh->r;
		stresses = stress_p + 6 * h;
		forces[stresses] -= neigh->dist.x * tmp_force.x;
		forces[stresses + 1] -= neigh->dist.y * tmp_force.y;
		forces[stresses + 2] -= neigh->dist.z * tmp_force.z;
		forces[stresses + 3] -= neigh->dist.x * tmp_force.y;
		forces[stresses + 4] -= neigh->dist.y * tmp_force.z;
		forces[stresses + 5] -= neigh->dist.z * tmp_force.x;
	      }
#endif /* STRESS */
	    }
	  }


	  /* dipole-monopole contributions */
	  if(xi_opt[2*size + ne + ntypes + typ1] && xi_opt[2*size + ne + typ2]) {
	    rp_i = SPROD(atom->p_ind, neigh->dist);
	    if (self && !(xi_opt[2*size + ne + typ1] && xi_opt[2*size + ne + ntypes + typ2])) {  
	      fnval_tail *= 0.5;
	      grad_tail *= 0.5;
	    }
	    
	    fnval = xi_opt[2*size + ne + typ2] * rp * fnval_tail;
	    grad_1 = xi_opt[2*size + ne + typ2] * rp * grad_tail;
	    grad_2 = xi_opt[2*size + ne + typ2] * fnval_tail;
	    	 	    
	    forces[energy_p + h] += fnval;
	    
	    if (uf) {
	      tmp_force.x = neigh->dist.x * grad_1 + atoms[neigh->nr].p_ind.x * grad_2;
	      tmp_force.y = neigh->dist.y * grad_1 + atoms[neigh->nr].p_ind.y * grad_2;
	      tmp_force.z = neigh->dist.z * grad_1 + atoms[neigh->nr].p_ind.z * grad_2;
	      forces[k] += tmp_force.x;
	      forces[k + 1] += tmp_force.y;
	      forces[k + 2] += tmp_force.z;
	      /* actio = reactio */
	      l = 3 * neigh->nr;
	      forces[l] -= tmp_force.x;
	      forces[l + 1] -= tmp_force.y;
	      forces[l + 2] -= tmp_force.z;
	      
#ifdef STRESS
	      /* calculate stresses */
	      if (us) {
		tmp_force.x *= neigh->r;
		tmp_force.y *= neigh->r;
		tmp_force.z *= neigh->r;
		stresses = stress_p + 6 * h;
		forces[stresses] -= neigh->dist.x * tmp_force.x;
		forces[stresses + 1] -= neigh->dist.y * tmp_force.y;
		forces[stresses + 2] -= neigh->dist.z * tmp_force.z;
		forces[stresses + 3] -= neigh->dist.x * tmp_force.y;
		forces[stresses + 4] -= neigh->dist.y * tmp_force.z;
		forces[stresses + 5] -= neigh->dist.z * tmp_force.x;
	      }
#endif /* STRESS */
	    }
	  }


	  /* dipole-dipole contributions */
	  if(xi_opt[2*size + ne + ntypes + typ1] && xi_opt[2*size + ne + ntypes + typ2]) {
	    
	    if(cs == 2) {
	    neigh->fnval_d = splint_comb_dir_d(&calc_pot, xi_d, 
					    neigh->slot[0],
					    neigh->shift[0],
					    neigh->step[0], &neigh->grad_d);
	    }

	    fnval_tail = neigh->fnval_d;
	    grad_tail = neigh->grad_d;

	    if (self) {  
	      fnval_tail *= 0.5;
	      grad_tail *= 0.5;
	    }
	    
	    rp_i = SPROD(atom->p_ind, neigh->dist);
	    rp_j = SPROD(atoms[neigh->nr].p_ind, neigh->dist);
	    pp_ij = SPROD(atom->p_ind, atoms[neigh->nr].p_ind);
	    tmp = 3 * rp_i * rp_j / neigh->r2;

	    fnval = (tmp - pp_ij) * fnval_tail;
	    grad_1 = (tmp - pp_ij) * grad_tail;
	    grad_2 = 3 * fnval_tail / neigh->r2;
	    grad_3 = 2 * tmp * fnval_tail/ neigh->r2;

	    forces[energy_p + h] += fnval;
	    
	    if (uf) {
	      tmp_force.x = neigh->dist.x * (grad_1 - grad_3)
		+ (rp_i * atoms[neigh->nr].p_ind.x + rp_j * atom->p_ind.x) * grad_2;
	      tmp_force.y = neigh->dist.y * (grad_1 - grad_3)
		+ (rp_i * atoms[neigh->nr].p_ind.y + rp_j * atom->p_ind.y) * grad_2;
	      tmp_force.z = neigh->dist.z * (grad_1 - grad_3)
		+ (rp_i * atoms[neigh->nr].p_ind.z + rp_j * atom->p_ind.z) * grad_2;
	      forces[k] += tmp_force.x;
	      forces[k + 1] += tmp_force.y;
	      forces[k + 2] += tmp_force.z;
	      /* actio = reactio */
	      l = 3 * neigh->nr;
	      forces[l] -= tmp_force.x;
	      forces[l + 1] -= tmp_force.y;
	      forces[l + 2] -= tmp_force.z;
	      
#ifdef STRESS
	      /* calculate stresses */
	      if (us) {
		tmp_force.x *= neigh->r;
		tmp_force.y *= neigh->r;
		tmp_force.z *= neigh->r;
		stresses = stress_p + 6 * h;
		forces[stresses] -= neigh->dist.x * tmp_force.x;
		forces[stresses + 1] -= neigh->dist.y * tmp_force.y;
		forces[stresses + 2] -= neigh->dist.z * tmp_force.z;
		forces[stresses + 3] -= neigh->dist.x * tmp_force.y;
		forces[stresses + 4] -= neigh->dist.y * tmp_force.z;
		forces[stresses + 5] -= neigh->dist.z * tmp_force.x;
	      }
#endif /* STRESS */
	    }
	  }


	    }
	  }       	/* loop over neighbours */
	}               /* end F O U R T H loop over atoms */ 
	

 	/* F I F T H  loop: self energy contributions */ 
	real qq, pp; 
 	for (i = 0; i < inconf[h]; i++) {    //atoms	  
	  atom = conf_atoms + i + cnfstart[h] - firstatom;
	  typ1 = atom->typ;
	  if (xi_opt[2*size + ne + typ1]) {
	    qq = xi_opt[2*size + ne + typ1] * xi_opt[2*size + ne + typ1];
	    fnval = dp_eps * dp_kappa * qq / sqrt(M_PI);
	    forces[energy_p + h] -= fnval;
	  }
	  if (xi_opt[2*size + ne + ntypes + typ1]) {
	    pp = SPROD(atom->p_ind, atom->p_ind);
	    fnval = pp / (2 * xi_opt[2*size + ne + ntypes + typ1]);
	    forces[energy_p + h] += fnval;
	  }
	}	/* end F I F T H loop */  
	

	/* energy contributions */
	forces[energy_p + h] *= eweight / (real)inconf[h];
	forces[energy_p + h] -= force_0[energy_p + h];
	tmpsum += conf_weight[h] * SQR(forces[energy_p + h]);
#ifdef STRESS
	/* stress contributions */
	if (uf && us) {
	  for (i = 0; i < 6; i++) {
	    forces[stress_p + 6 * h + i] *= sweight / conf_vol[h - firstconf];
	    forces[stress_p + 6 * h + i] -= force_0[stress_p + 6 * h + i];
	    tmpsum += conf_weight[h] * SQR(forces[stress_p + 6 * h + i]);
	  }
	}
#endif /* STRESS */	


      }				/* end M A I N loop over configurations */

      /* output for "Dipol_Konvergenz_Verlauf" */
      sum_t = sum_c / h;
      outfile2 = fopen(filename2, "a");
      fprintf(outfile2, "%d\n", sum_t);
      fclose(outfile2);	
     
    }				/* parallel region */
    
    /* dummy constraints (global) */
#ifdef APOT
    /* add punishment for out of bounds (mostly for powell_lsq) */
    if (myid == 0) {
      tmpsum += apot_punish(xi_opt, forces);
    }
#endif

    sum = tmpsum;		/* global sum = local sum  */

#ifdef MPI
    /* reduce global sum */
    sum = 0.;
    MPI_Reduce(&tmpsum, &sum, 1, REAL, MPI_SUM, 0, MPI_COMM_WORLD);
    /* gather forces, energies, stresses */
    MPI_Gatherv(forces + firstatom * 3, myatoms, MPI_VEKTOR,	/* forces */
		forces, atom_len, atom_dist, MPI_VEKTOR, 0, MPI_COMM_WORLD);
    MPI_Gatherv(forces + natoms * 3 + firstconf, myconf, REAL,	/* energies */
		forces
		+ natoms * 3, conf_len, conf_dist, REAL, 0, MPI_COMM_WORLD);
    /* stresses */
    MPI_Gatherv(forces + natoms * 3 + nconf +
		6 * firstconf, myconf, MPI_STENS,
		forces + natoms * 3 + nconf,
		conf_len, conf_dist, MPI_STENS, 0, MPI_COMM_WORLD);
#endif /* MPI */

    /* root process exits this function now */
    if (myid == 0) {
      fcalls++;			/* Increase function call counter */
      if (isnan(sum)) {
#ifdef DEBUG
	printf("\n--> Force is nan! <--\n\n");
#endif
	return 10e10;
      } else
	return sum;
    }

  }

  /* once a non-root process arrives here, all is done. */
  return -1.;
}

#endif /* DIPOLE */
