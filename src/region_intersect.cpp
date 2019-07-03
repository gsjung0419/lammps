/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "region_intersect.h"
#include <cstdlib>
#include <cstring>
#include "domain.h"
#include "error.h"
#include "force.h"

using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

RegIntersect::RegIntersect(LAMMPS *lmp, int narg, char **arg) :
  Region(lmp, narg, arg), idsub(NULL)
{
  nregion = 0;

  if (narg < 5) error->all(FLERR,"Illegal region command");
  int n = force->inumeric(FLERR,arg[2]);
  if (n < 2) error->all(FLERR,"Illegal region command");
  options(narg-(n+3),&arg[n+3]);

  // build list of regions to intersect
  // store sub-region IDs in idsub

  idsub = new char*[n];
  list = new int[n];
  nregion = 0;

  int m,iregion;
  for (int iarg = 0; iarg < n; iarg++) {
    m = strlen(arg[iarg+3]) + 1;
    idsub[nregion] = new char[m];
    strcpy(idsub[nregion],arg[iarg+3]);
    iregion = domain->find_region(idsub[nregion]);
    if (iregion == -1)
      error->all(FLERR,"Region intersect region ID does not exist");
    list[nregion++] = iregion;
  }

  // this region is variable shape or dynamic if any of sub-regions are

  Region **regions = domain->regions;
  for (int ilist = 0; ilist < nregion; ilist++) {
    if (regions[list[ilist]]->varshape) varshape = 1;
    if (regions[list[ilist]]->dynamic) dynamic = 1;
  }

  // extent of intersection of regions
  // has bounding box if interior and any sub-region has bounding box

  bboxflag = 0;
  for (int ilist = 0; ilist < nregion; ilist++)
    if (regions[list[ilist]]->bboxflag == 1) bboxflag = 1;
  if (!interior) bboxflag = 0;

  if (bboxflag) {
    int first = 1;
    for (int ilist = 0; ilist < nregion; ilist++) {
      if (regions[list[ilist]]->bboxflag == 0) continue;
      if (first) {
        extent_xlo = regions[list[ilist]]->extent_xlo;
        extent_ylo = regions[list[ilist]]->extent_ylo;
        extent_zlo = regions[list[ilist]]->extent_zlo;
        extent_xhi = regions[list[ilist]]->extent_xhi;
        extent_yhi = regions[list[ilist]]->extent_yhi;
        extent_zhi = regions[list[ilist]]->extent_zhi;
        first = 0;
      }

      extent_xlo = MAX(extent_xlo,regions[list[ilist]]->extent_xlo);
      extent_ylo = MAX(extent_ylo,regions[list[ilist]]->extent_ylo);
      extent_zlo = MAX(extent_zlo,regions[list[ilist]]->extent_zlo);
      extent_xhi = MIN(extent_xhi,regions[list[ilist]]->extent_xhi);
      extent_yhi = MIN(extent_yhi,regions[list[ilist]]->extent_yhi);
      extent_zhi = MIN(extent_zhi,regions[list[ilist]]->extent_zhi);
    }
  }

  // possible contacts = sum of possible contacts in all sub-regions
  // for near contacts and touching contacts

  cmax = 0;
  for (int ilist = 0; ilist < nregion; ilist++)
    cmax += regions[list[ilist]]->cmax;
  contact = new Contact[cmax];

  tmax = 0;
  for (int ilist = 0; ilist < nregion; ilist++) {
    if (interior) tmax += regions[list[ilist]]->tmax;
    else tmax++;
  }
}

/* ---------------------------------------------------------------------- */

RegIntersect::~RegIntersect()
{
  for (int ilist = 0; ilist < nregion; ilist++) delete [] idsub[ilist];
  delete [] idsub;
  delete [] list;
  delete [] contact;
}

/* ---------------------------------------------------------------------- */

void RegIntersect::init()
{
  Region::init();

  // re-build list of sub-regions in case other regions were deleted
  // error if a sub-region was deleted

  int iregion;
  for (int ilist = 0; ilist < nregion; ilist++) {
    iregion = domain->find_region(idsub[ilist]);
    if (iregion == -1)
      error->all(FLERR,"Region union region ID does not exist");
    list[ilist] = iregion;
  }

  // init the sub-regions

  Region **regions = domain->regions;
  for (int ilist = 0; ilist < nregion; ilist++)
    regions[list[ilist]]->init();
}

/* ----------------------------------------------------------------------
   inside = 1 if x,y,z is match() with all sub-regions
   else inside = 0
------------------------------------------------------------------------- */

int RegIntersect::inside(double x, double y, double z)
{
  int ilist;
  Region **regions = domain->regions;
  for (ilist = 0; ilist < nregion; ilist++)
    if (!regions[list[ilist]]->match(x,y,z)) break;

  if (ilist == nregion) return 1;
  return 0;
}

/* ----------------------------------------------------------------------
   compute contacts with interior of intersection of sub-regions
   (1) compute contacts in each sub-region
   (2) only keep a contact if surface point is match() to all other regions
------------------------------------------------------------------------- */

int RegIntersect::surface_interior(double *x, double cutoff)
{
  int m,ilist,jlist,iregion,jregion,ncontacts;
  double xs,ys,zs;

  Region **regions = domain->regions;
  int n = 0;

  int walloffset = 0;
  for (ilist = 0; ilist < nregion; ilist++) {
    iregion = list[ilist];
    ncontacts = regions[iregion]->surface(x[0],x[1],x[2],cutoff);
    for (m = 0; m < ncontacts; m++) {
      xs = x[0] - regions[iregion]->contact[m].delx;
      ys = x[1] - regions[iregion]->contact[m].dely;
      zs = x[2] - regions[iregion]->contact[m].delz;
      for (jlist = 0; jlist < nregion; jlist++) {
        if (jlist == ilist) continue;
        jregion = list[jlist];
        if (!regions[jregion]->match(xs,ys,zs)) break;
      }
      if (jlist == nregion) {
        contact[n].r = regions[iregion]->contact[m].r;
        contact[n].radius = regions[iregion]->contact[m].radius;
        contact[n].delx = regions[iregion]->contact[m].delx;
        contact[n].dely = regions[iregion]->contact[m].dely;
        contact[n].delz = regions[iregion]->contact[m].delz;
        contact[n].iwall = regions[iregion]->contact[m].iwall + walloffset;
        contact[n].varflag = regions[iregion]->contact[m].varflag;
        n++;
      }
    }
    // increment by cmax instead of tmax to insure
    // possible wall IDs for sub-regions are non overlapping
    walloffset += regions[iregion]->cmax;
  }

  return n;
}

/* ----------------------------------------------------------------------
   compute contacts with interior of intersection of sub-regions
   (1) flip interior/exterior flag of each sub-region
   (2) compute contacts in each sub-region
   (3) only keep a contact if surface point is not match() to all other regions
   (4) flip interior/exterior flags back to original settings
   this is effectively same algorithm as surface_interior() for RegUnion
------------------------------------------------------------------------- */

int RegIntersect::surface_exterior(double *x, double cutoff)
{
  int m,ilist,jlist,iregion,jregion,ncontacts;
  double xs,ys,zs;

  Region **regions = domain->regions;
  int n = 0;

  for (ilist = 0; ilist < nregion; ilist++)
    regions[list[ilist]]->interior ^= 1;

  for (ilist = 0; ilist < nregion; ilist++) {
    iregion = list[ilist];
    ncontacts = regions[iregion]->surface(x[0],x[1],x[2],cutoff);
    for (m = 0; m < ncontacts; m++) {
      xs = x[0] - regions[iregion]->contact[m].delx;
      ys = x[1] - regions[iregion]->contact[m].dely;
      zs = x[2] - regions[iregion]->contact[m].delz;
      for (jlist = 0; jlist < nregion; jlist++) {
        if (jlist == ilist) continue;
        jregion = list[jlist];
        if (regions[jregion]->match(xs,ys,zs)) break;
      }
      if (jlist == nregion) {
        contact[n].r = regions[iregion]->contact[m].r;
        contact[n].radius = regions[iregion]->contact[m].radius;
        contact[n].delx = regions[iregion]->contact[m].delx;
        contact[n].dely = regions[iregion]->contact[m].dely;
        contact[n].delz = regions[iregion]->contact[m].delz;
        contact[n].iwall = ilist;
        contact[n].varflag = regions[iregion]->contact[m].varflag;
        n++;
      }
    }
  }

  for (ilist = 0; ilist < nregion; ilist++)
    regions[list[ilist]]->interior ^= 1;

  return n;
}

/* ----------------------------------------------------------------------
   change region shape of all sub-regions
------------------------------------------------------------------------- */

void RegIntersect::shape_update()
{
  Region **regions = domain->regions;
  for (int ilist = 0; ilist < nregion; ilist++)
    regions[list[ilist]]->shape_update();
}

/* ----------------------------------------------------------------------
   move/rotate all sub-regions
------------------------------------------------------------------------- */

void RegIntersect::pretransform()
{
  Region **regions = domain->regions;
  for (int ilist = 0; ilist < nregion; ilist++)
    regions[list[ilist]]->pretransform();
}


/* ----------------------------------------------------------------------
   get translational/angular velocities of all subregions
------------------------------------------------------------------------- */

void RegIntersect::set_velocity()
{
  Region **regions = domain->regions;
  for (int ilist = 0; ilist < nregion; ilist++)
    regions[list[ilist]]->set_velocity();
}

/* ----------------------------------------------------------------------
   increment length of restart buffer based on region info
   used by restart of fix/wall/gran/region
------------------------------------------------------------------------- */

void RegIntersect::length_restart_string(int& n)
{
  n += sizeof(int) + strlen(id)+1 +
    sizeof(int) + strlen(style)+1 + sizeof(int);
  for (int ilist = 0; ilist < nregion; ilist++)
    domain->regions[list[ilist]]->length_restart_string(n);

}
/* ----------------------------------------------------------------------
   region writes its current position/angle
   needed by fix/wall/gran/region to compute velocity by differencing scheme
------------------------------------------------------------------------- */

void RegIntersect::write_restart(FILE *fp)
{
  int sizeid = (strlen(id)+1);
  int sizestyle = (strlen(style)+1);
  fwrite(&sizeid, sizeof(int), 1, fp);
  fwrite(id, 1, sizeid, fp);
  fwrite(&sizestyle, sizeof(int), 1, fp);
  fwrite(style, 1, sizestyle, fp);
  fwrite(&nregion,sizeof(int),1,fp);

  for (int ilist = 0; ilist < nregion; ilist++){
    domain->regions[list[ilist]]->write_restart(fp);
  }
}

/* ----------------------------------------------------------------------
   region reads its previous position/angle
   needed by fix/wall/gran/region to compute velocity by differencing scheme
------------------------------------------------------------------------- */

int RegIntersect::restart(char *buf, int &n)
{
  int size = *((int *) (&buf[n]));
  n += sizeof(int);
  if ((size <= 0) || (strcmp(&buf[n],id) != 0)) return 0;
  n += size;

  size = *((int *) (&buf[n]));
  n += sizeof(int);
  if ((size <= 0) || (strcmp(&buf[n],style) != 0)) return 0;
  n += size;

  int restart_nreg = *((int *) (&buf[n]));
  n += sizeof(int);
  if (restart_nreg != nregion) return 0;

  for (int ilist = 0; ilist < nregion; ilist++)
    if (!domain->regions[list[ilist]]->restart(buf,n)) return 0;

  return 1;
}

/* ----------------------------------------------------------------------
   set prev vector to zero
------------------------------------------------------------------------- */

void RegIntersect::reset_vel()
{
  for (int ilist = 0; ilist < nregion; ilist++)
    domain->regions[list[ilist]]->reset_vel();
}

