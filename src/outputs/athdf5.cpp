//======================================================================================
// Athena++ astrophysical MHD code
// Copyright (C) 2014 James M. Stone  <jmstone@princeton.edu>
//
// This program is free software: you can redistribute and/or modify it under the terms
// of the GNU General Public License (GPL) as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of GNU GPL in the file LICENSE included in the code
// distribution.  If not see <http://www.gnu.org/licenses/>.
//======================================================================================

#include "../athena.hpp"

#ifdef HDF5OUTPUT

#include <sstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <hdf5.h>

#include "../athena_arrays.hpp"
#include "../globals.hpp"
#include "../mesh.hpp"
#include "../parameter_input.hpp"
#include "../hydro/hydro.hpp"
#include "../field/field.hpp"
#include "outputs.hpp"
#include "../coordinates/coordinates.hpp" // Coordinates

//======================================================================================
//! \file athdf5.cpp
//  \brief writes Athena HDF5 (.athdf) files. note: C binding is used.
//======================================================================================

ATHDF5Output::ATHDF5Output(OutputParameters oparams)
  : OutputType(oparams)
{
}


//--------------------------------------------------------------------------------------
//! \fn void ATHDF5Output::Initialize(Mesh *pM, ParameterInput *pin, bool wtflag)
//  \brief open the Athena HDF5 file, create the meta data for the whole Mesh
void ATHDF5Output::Initialize(Mesh *pM, ParameterInput *pin, bool wtflag=false)
{
  std::string fname;
  hid_t aid, dsid, tid, tgid;
  hsize_t sz;
  long int lx[3];
  int ll;
  int nbs=pM->nslist[Globals::my_rank];
  int nbe=nbs+pM->nblist[Globals::my_rank]-1;
  int nbl=nbe-nbs+1;

  // create single output, filename:"file_basename"+"."+"file_id"+"."+XXXXX+".athdf",
  // where XXXXX = 5-digit file_number
  char number[6]; // array to store 4-digit number and end-of-string char
  sprintf(number,"%05d",output_params.file_number);
  fname.assign(output_params.file_basename);
  fname.append(".");
  fname.append(output_params.file_id);
  fname.append(".");
  fname.append(number);
  fname.append(".athdf");

  // create a new file
#ifdef MPI_PARALLEL
  // Set up parameters for parallel IO
  hid_t plist;
  plist = H5Pcreate(H5P_FILE_ACCESS);
  H5Pset_fapl_mpio(plist, MPI_COMM_WORLD, MPI_INFO_NULL);
  file= H5Fcreate(fname.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, plist);
  if(file==-1) {
   std::cout << "error, failed to open a file " << fname << std::endl;
   return;
  }
  H5Pclose(plist);
#else
  // serial: everything is default
  file= H5Fcreate(fname.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
#endif

  // setup metadata
  sz=1;
  dsid = H5Screate_simple(1, &sz, NULL);
  aid=H5Acreate2(file,"TotalMeshBlock",H5T_STD_I32BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
  H5Awrite(aid, H5T_NATIVE_INT, &(pM->nbtotal));
  H5Aclose(aid); H5Sclose(dsid);
  // meshblock size
  dim=1;
  mbsize[0]=pM->pblock->block_size.nx1;
  mbsize[1]=pM->pblock->block_size.nx2;
  mbsize[2]=pM->pblock->block_size.nx3;
  if(mbsize[1]>1) dim=2;
  if(mbsize[2]>1) dim=3;

  if(dim==1) dims[0]=mbsize[0], dims[1]=1, dims[2]=1;
  if(dim==2) dims[0]=mbsize[1], dims[1]=mbsize[0], dims[2]=1;
  if(dim==3) dims[0]=mbsize[2], dims[1]=mbsize[1], dims[2]=mbsize[0];

  int rgs[3];
  rgs[0]=pM->mesh_size.nx1, rgs[1]=pM->mesh_size.nx2, rgs[2]=pM->mesh_size.nx3;

  // MeshBlockSize
  sz=3;
  dsid = H5Screate_simple(1, &sz, NULL);
  aid=H5Acreate2(file,"MeshBlockSize",H5T_STD_I32BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
  H5Awrite(aid, H5T_NATIVE_INT, mbsize);
  H5Aclose(aid); H5Sclose(dsid);
  // RootGridSize
  sz=3;
  dsid = H5Screate_simple(1, &sz, NULL);
  aid=H5Acreate2(file,"RootGridSize",H5T_STD_I32BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
  H5Awrite(aid, H5T_NATIVE_INT, rgs);
  H5Aclose(aid); H5Sclose(dsid);
  // MaxLevel
  int ml=pM->current_level - pM->root_level;
  sz=1;
  dsid = H5Screate_simple(1, &sz, NULL);
  aid=H5Acreate2(file,"MaxLevel",H5T_STD_I32BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
  H5Awrite(aid, H5T_NATIVE_INT, &ml);
  H5Aclose(aid); H5Sclose(dsid);
  // ncycle
  sz=1;
  dsid = H5Screate_simple(1, &sz, NULL);
  aid=H5Acreate2(file,"NCycle",H5T_STD_I32BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
  H5Awrite(aid, H5T_NATIVE_INT, &(pM->ncycle));
  H5Aclose(aid); H5Sclose(dsid);
  // time
  sz=1;
  dsid = H5Screate_simple(1, &sz, NULL);
  aid=H5Acreate2(file,"Time",H5T_IEEE_F64BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
  H5Awrite(aid, H5T_NATIVE_DOUBLE, &(pM->time));
  H5Aclose(aid); H5Sclose(dsid);
  // number of variables
  sz=1;
  dsid = H5Screate_simple(1, &sz, NULL);
  aid=H5Acreate2(file,"NVariables",H5T_STD_I32BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
  H5Awrite(aid, H5T_NATIVE_INT, &var_added);
  H5Aclose(aid); H5Sclose(dsid);

  grpid = new hid_t[nbl];
  x1fid = new hid_t[nbl];
  x2fid = new hid_t[nbl];
  if(mbsize[2]>1)
    x3fid = new hid_t[nbl];
  rhoid = new hid_t[nbl];
  if (NON_BAROTROPIC_EOS)
    eid = new hid_t[nbl];
  if (RADIATION_ENABLED){
    erid = new hid_t[nbl];
    er0id = new hid_t[nbl];
    sigmasid = new hid_t[nbl];
    sigmaaid = new hid_t[nbl];
    for(int n=0; n<3; n++){
      frid[n] = new hid_t[nbl];
      fr0id[n] = new hid_t[nbl];
    }
    
    for(int n=0; n<9; n++)
      prid[n] = new hid_t[nbl];
  }
  for(int n=0;n<3;n++) {
    mid[n] = new hid_t[nbl];
    if (MAGNETIC_FIELDS_ENABLED)
      bid[n] = new hid_t[nbl];
  }
  if (NIFOV > 0)
    ifovid = new hid_t *[NIFOV];
  for(int n=0;n<NIFOV;n++)
    ifovid[n]=new hid_t [nbl];

  for(int b=0;b<pM->nbtotal;b++) {
    // create groups for all the MeshBlocks
    std::string gname="/MeshBlock";
    char mbid[8];
    sprintf(mbid,"%d",b);
    gname.append(mbid);
    tgid = H5Gcreate(file, gname.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    lx[0]=pM->loclist[b].lx1;
    lx[1]=pM->loclist[b].lx2;
    lx[2]=pM->loclist[b].lx3;
    int level = pM->loclist[b].level-pM->root_level;
    sz=1;
    dsid = H5Screate_simple(1, &sz, NULL);
    aid=H5Acreate2(tgid,"Level",H5T_STD_I32BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
    H5Awrite(aid, H5T_NATIVE_INT, &level);
    H5Aclose(aid); H5Sclose(dsid);
    sz=3;
    dsid = H5Screate_simple(1, &sz, NULL);
    aid=H5Acreate2(tgid,"LogicalLocation",H5T_STD_I64BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
    H5Awrite(aid, H5T_NATIVE_LONG, lx);
    H5Aclose(aid); H5Sclose(dsid);
    sz=1;
    dsid = H5Screate_simple(1, &sz, NULL);
    aid=H5Acreate2(tgid,"GlobalID",H5T_STD_I32BE,dsid,H5P_DEFAULT,H5P_DEFAULT);
    H5Awrite(aid, H5T_NATIVE_INT, &b);
    H5Aclose(aid); H5Sclose(dsid);

    sz=mbsize[0]+1;
    dsid = H5Screate_simple(1, &sz, NULL);
    tid = H5Dcreate2(tgid,"x1f",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
    if(b>=nbs && b<=nbe) x1fid[b-nbs]=tid;
    else H5Dclose(tid);
    H5Sclose(dsid);
    sz=mbsize[1]+1;
    dsid = H5Screate_simple(1, &sz, NULL);
    tid = H5Dcreate2(tgid,"x2f",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
    if(b>=nbs && b<=nbe) x2fid[b-nbs]=tid;
    else H5Dclose(tid);
    H5Sclose(dsid);
    if(mbsize[2]>1) {
      sz=mbsize[2]+1;
      dsid = H5Screate_simple(1, &sz, NULL);
      tid = H5Dcreate2(tgid,"x3f",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
      if(b>=nbs && b<=nbe) x3fid[b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
    }

    if (output_params.variable.compare("D") == 0 || 
        output_params.variable.compare("cons") == 0) {
      dsid = H5Screate_simple(dim, dims, NULL);
      tid = H5Dcreate2(tgid,"dens",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
      if(b>=nbs && b<=nbe) rhoid[b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
    }

    if (output_params.variable.compare("d") == 0 || 
        output_params.variable.compare("prim") == 0) {
      dsid = H5Screate_simple(dim, dims, NULL);
      tid = H5Dcreate2(tgid,"rho",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
      if(b>=nbs && b<=nbe) rhoid[b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
    }

    if (NON_BAROTROPIC_EOS) {
      if (output_params.variable.compare("E") == 0 || 
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Etot",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
        if(b>=nbs && b<=nbe) eid[b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }

      if (output_params.variable.compare("p") == 0 || 
          output_params.variable.compare("prim") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"press",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
        if(b>=nbs && b<=nbe) eid[b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
    }

    if (output_params.variable.compare("m") == 0 || 
        output_params.variable.compare("cons") == 0) {
      dsid = H5Screate_simple(dim, dims, NULL);
      tid = H5Dcreate2(tgid,"mom1",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
      if(b>=nbs && b<=nbe) mid[0][b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
      dsid = H5Screate_simple(dim, dims, NULL);
      tid = H5Dcreate2(tgid,"mom2",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
      if(b>=nbs && b<=nbe) mid[1][b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
      dsid = H5Screate_simple(dim, dims, NULL);
      tid = H5Dcreate2(tgid,"mom3",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
      if(b>=nbs && b<=nbe) mid[2][b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
    }

    if (output_params.variable.compare("v") == 0 || 
        output_params.variable.compare("prim") == 0) {
      dsid = H5Screate_simple(dim, dims, NULL);
      tid = H5Dcreate2(tgid,"vel1",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
      if(b>=nbs && b<=nbe) mid[0][b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
      dsid = H5Screate_simple(dim, dims, NULL);
      tid = H5Dcreate2(tgid,"vel2",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                       H5P_DEFAULT);
      if(b>=nbs && b<=nbe) mid[1][b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
      dsid = H5Screate_simple(dim, dims, NULL);
      tid = H5Dcreate2(tgid,"vel3",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                       H5P_DEFAULT);
      if(b>=nbs && b<=nbe) mid[2][b-nbs]=tid;
      else H5Dclose(tid);
      H5Sclose(dsid);
    }

    if (MAGNETIC_FIELDS_ENABLED) {
      if (output_params.variable.compare("b") == 0 || 
          output_params.variable.compare("prim") == 0 ||
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"cc-B1",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                        H5P_DEFAULT);
        if(b>=nbs && b<=nbe) bid[0][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"cc-B2",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                         H5P_DEFAULT);
        if(b>=nbs && b<=nbe) bid[1][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"cc-B3",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                         H5P_DEFAULT);
        if(b>=nbs && b<=nbe) bid[2][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
    }
    if (RADIATION_ENABLED){
      if (output_params.variable.compare("Er") == 0 ||
          output_params.variable.compare("prim") == 0 ||
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Er",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                         H5P_DEFAULT);
        if(b>=nbs && b<=nbe) erid[b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
      
      if (output_params.variable.compare("Er0") == 0 ||
          output_params.variable.compare("prim") == 0 ||
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Er0",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                         H5P_DEFAULT);
        if(b>=nbs && b<=nbe) er0id[b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
      
      if (output_params.variable.compare("Sigma_s") == 0 ||
          output_params.variable.compare("prim") == 0 ||
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Sigma_s",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                         H5P_DEFAULT);
        if(b>=nbs && b<=nbe) sigmasid[b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
      if (output_params.variable.compare("Sigma_a") == 0 ||
          output_params.variable.compare("prim") == 0 ||
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Sigma_a",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
                         H5P_DEFAULT);
        if(b>=nbs && b<=nbe) sigmaaid[b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
      if (output_params.variable.compare("Fr") == 0 ||
          output_params.variable.compare("prim") == 0 ||
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Fr1",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) frid[0][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Fr2",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) frid[1][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Fr3",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) frid[2][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
      
      if (output_params.variable.compare("Fr0") == 0 ||
          output_params.variable.compare("prim") == 0 ||
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Fr01",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) fr0id[0][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Fr02",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) fr0id[1][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Fr03",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) fr0id[2][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
      
      if (output_params.variable.compare("Pr") == 0 ||
          output_params.variable.compare("prim") == 0 ||
          output_params.variable.compare("cons") == 0) {
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr11",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[0][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr12",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[1][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr13",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[2][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr21",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[3][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr22",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[4][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr23",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[5][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr31",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[6][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr32",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[7][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,"Pr33",H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) prid[8][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
      
    }
    if (output_params.variable.compare("ifov") == 0) {
      for (int n=0; n<(NIFOV); ++n) {
        std::string iname="ifov";
        char num[4];
        sprintf(num,"%d",n);
        iname.append(num);
        dsid = H5Screate_simple(dim, dims, NULL);
        tid = H5Dcreate2(tgid,iname.c_str(),H5T_IEEE_F32BE,dsid,H5P_DEFAULT,H5P_DEFAULT,
              H5P_DEFAULT);
        if(b>=nbs && b<=nbe) ifovid[n][b-nbs]=tid;
        else H5Dclose(tid);
        H5Sclose(dsid);
      }
    }
    if(b>=nbs && b<=nbe) grpid[b-nbs]=tgid;
    else H5Gclose(tgid);
  }

  if(Globals::my_rank==0 && pin->GetOrAddInteger(output_params.block_name,"xdmf",1)!=0) {
    std::string xname=fname;
    xname.append(".xdmf");
    std::ofstream xdmf(xname.c_str());
    xdmf << "<?xml version=\"1.0\" ?>" << std::endl
         << "<!DOCTYPE Xdmf SYSTEM \"Xdmf.dtd\" []>" << std::endl
         << "<Xdmf Version=\"2.0\">" << std::endl << "<Domain>" << std::endl
         << "<Grid Name=\"Mesh\" GridType=\"Collection\">" << std::endl;
         
    char sdim[32];
    if(mbsize[2]==1) sprintf(sdim,"%d %d", mbsize[1], mbsize[0]);
    else sprintf(sdim,"%d %d %d", mbsize[2], mbsize[1], mbsize[0]);

    for(int b=0;b<pM->nbtotal;b++) { // each MeshBlock
      char bn[32];
      sprintf(bn,"MeshBlock%d",b);
      xdmf << "  <Grid Name=\""<< bn << "\" GridType=\"Uniform\">" << std::endl;

      // coordinates
      if(mbsize[2]==1) { // 1D or 2D
        xdmf << "    <Topology TopologyType=\"2DRectMesh\" NumberOfElements=\""
             << mbsize[1]+1 << " "<< mbsize[0]+1 <<"\"/>" << std::endl
             << "    <Geometry GeometryType=\"VXVY\">" << std::endl
             << "      <DataItem Dimensions=\"" << mbsize[0]+1 << "\" "
             << "NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
             << fname << ":/" << bn << "/x1f" << "</DataItem>" << std::endl
             << "      <DataItem Dimensions=\"" << mbsize[1]+1 << "\" "
             << "NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
             << fname << ":/" << bn << "/x2f" << "</DataItem>" << std::endl;
      }
      else {
        xdmf << "    <Topology TopologyType=\"3DRectMesh\" NumberOfElements=\""
             << mbsize[2]+1 << " " << mbsize[1]+1 << " "<< mbsize[0]+1 <<"\"/>"
             << std::endl
             << "      <Geometry GeometryType=\"VXVYVZ\">" << std::endl
             << "      <DataItem Dimensions=\"" << mbsize[0]+1 << "\" "
             << "NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
             << fname << ":/" << bn << "/x1f" << "</DataItem>" << std::endl
             << "      <DataItem Dimensions=\"" << mbsize[1]+1 << "\" "
             << "NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
             << fname << ":/" << bn << "/x2f" << "</DataItem>" << std::endl
             << "      <DataItem Dimensions=\"" << mbsize[2]+1 << "\" "
             << "NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
             << fname << ":/" << bn << "/x3f" << "</DataItem>" << std::endl;
      }
      xdmf << "    </Geometry>" << std::endl;
      if (output_params.variable.compare("D") == 0 || 
          output_params.variable.compare("cons") == 0) {
          xdmf << "    <Attribute Name=\"Density\" AttributeType=\"Scalar\" "
               << "Center=\"Cell\">" << std::endl
               << "      <DataItem Dimensions=\"" << sdim << "\" NumberType=\"Float\" "
               << "Precision=\"4\" Format=\"HDF\">" << fname << ":/" << bn << "/dens"
               << "</DataItem>" << std::endl << "    </Attribute>" << std::endl;
      }

      if (output_params.variable.compare("d") == 0 || 
          output_params.variable.compare("prim") == 0) {
          xdmf << "    <Attribute Name=\"gas_density\" AttributeType=\"Scalar\" "
               << "Center=\"Cell\">" << std::endl
               << "      <DataItem Dimensions=\"" << sdim << "\" NumberType=\"Float\" "
               << "Precision=\"4\" Format=\"HDF\">" << fname << ":/" << bn << "/rho"
               << "</DataItem>" << std::endl << "    </Attribute>" << std::endl;
      }

      if (NON_BAROTROPIC_EOS) {
        if (output_params.variable.compare("E") == 0 || 
            output_params.variable.compare("cons") == 0) {
            xdmf << "    <Attribute Name=\"total_energy\" AttributeType=\"Scalar\" "
                 << "Center=\"Cell\">" << std::endl
                 << "      <DataItem Dimensions=\"" << sdim << "\" NumberType=\"Float\" "
                 << "Precision=\"4\" Format=\"HDF\">" << fname << ":/" << bn << "/Etot"
                 << "</DataItem>" << std::endl << "    </Attribute>" << std::endl;
        }

        if (output_params.variable.compare("p") == 0 || 
            output_params.variable.compare("prim") == 0) {
            xdmf << "    <Attribute Name=\"gas_pressure\" AttributeType=\"Scalar\" "
                 << "Center=\"Cell\">" << std::endl
                 << "      <DataItem Dimensions=\"" << sdim << "\" NumberType=\"Float\" "
                 << "Precision=\"4\" Format=\"HDF\">" << fname << ":/" << bn << "/press"
                 << "</DataItem>" << std::endl << "    </Attribute>" << std::endl;
        }
      }

      if (output_params.variable.compare("m") == 0 || 
          output_params.variable.compare("cons") == 0) {
            xdmf << "    <Attribute Name=\"gas_momentum_x1\" AttributeType=\"Scalar\" "
                 << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                 << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                 << fname << ":/" << bn << "/mom1" << "</DataItem>" << std::endl
                 << "    </Attribute>" << std::endl;
            xdmf << "    <Attribute Name=\"gas_momentum_x2\" AttributeType=\"Scalar\" "
                 << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                 << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                 << fname << ":/" << bn << "/mom2" << "</DataItem>" << std::endl
                 << "    </Attribute>" << std::endl;
            xdmf << "    <Attribute Name=\"gas_momentum_x3\" AttributeType=\"Scalar\" "
                 << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                 << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                 << fname << ":/" << bn << "/mom3" << "</DataItem>" << std::endl
                 << "    </Attribute>" << std::endl;
      }

      if (output_params.variable.compare("v") == 0 || 
          output_params.variable.compare("prim") == 0) {
            xdmf << "    <Attribute Name=\"gas_velocity_x1\" AttributeType=\"Scalar\" "
                 << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                 << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                 << fname << ":/" << bn << "/vel1" << "</DataItem>" << std::endl
                 << "    </Attribute>" << std::endl;
            xdmf << "    <Attribute Name=\"gas_velocity_x2\" AttributeType=\"Scalar\" "
                 << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                 << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                 << fname << ":/" << bn << "/vel2" << "</DataItem>" << std::endl
                 << "    </Attribute>" << std::endl;
            xdmf << "    <Attribute Name=\"gas_velocity_x3\" AttributeType=\"Scalar\" "
                 << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                 << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                 << fname << ":/" << bn << "/vel3" << "</DataItem>" << std::endl
                 << "    </Attribute>" << std::endl;
      }

      if (MAGNETIC_FIELDS_ENABLED) {
        if (output_params.variable.compare("b") == 0 || 
            output_params.variable.compare("prim") == 0 ||
            output_params.variable.compare("cons") == 0) {
              xdmf << "    <Attribute Name=\"bfield_x1\" AttributeType=\"Scalar\" "
                   << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                   << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                   << fname << ":/" << bn << "/cc-B1" << "</DataItem>" << std::endl
                   << "    </Attribute>" << std::endl;
              xdmf << "    <Attribute Name=\"bfield_x2\" AttributeType=\"Scalar\" "
                   << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                   << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                   << fname << ":/" << bn << "/cc-B2" << "</DataItem>" << std::endl
                   << "    </Attribute>" << std::endl;
              xdmf << "    <Attribute Name=\"bfield_x3\" AttributeType=\"Scalar\" "
                   << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
                   << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
                   << fname << ":/" << bn << "/cc-B3" << "</DataItem>" << std::endl
                   << "    </Attribute>" << std::endl;
        }
      }
      if (RADIATION_ENABLED){
        if (output_params.variable.compare("Er") == 0 ||
            output_params.variable.compare("prim") == 0 ||
            output_params.variable.compare("cons") == 0) {
          xdmf << "    <Attribute Name=\"radiation_energy\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl
          << "      <DataItem Dimensions=\"" << sdim << "\" NumberType=\"Float\" "
          << "Precision=\"4\" Format=\"HDF\">" << fname << ":/" << bn << "/Er"
          << "</DataItem>" << std::endl << "    </Attribute>" << std::endl;
        }
        if (output_params.variable.compare("Sigma_s") == 0 ||
            output_params.variable.compare("prim") == 0 ||
            output_params.variable.compare("cons") == 0) {
          xdmf << "    <Attribute Name=\"scattering_opacity\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl
          << "      <DataItem Dimensions=\"" << sdim << "\" NumberType=\"Float\" "
          << "Precision=\"4\" Format=\"HDF\">" << fname << ":/" << bn << "/Sigma_s"
          << "</DataItem>" << std::endl << "    </Attribute>" << std::endl;
        }
        if (output_params.variable.compare("Sigma_a") == 0 ||
            output_params.variable.compare("prim") == 0 ||
            output_params.variable.compare("cons") == 0) {
          xdmf << "    <Attribute Name=\"absorption_opacity\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl
          << "      <DataItem Dimensions=\"" << sdim << "\" NumberType=\"Float\" "
          << "Precision=\"4\" Format=\"HDF\">" << fname << ":/" << bn << "/Sigma_a"
          << "</DataItem>" << std::endl << "    </Attribute>" << std::endl;
        }
        if (output_params.variable.compare("Fr") == 0 ||
            output_params.variable.compare("prim") == 0 ||
            output_params.variable.compare("cons") == 0) {
          xdmf << "    <Attribute Name=\"radiation_flux_x1\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Fr1" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_flux_x2\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Fr2" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_flux_x3\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Fr3" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
        }
        if (output_params.variable.compare("Pr") == 0 ||
            output_params.variable.compare("prim") == 0 ||
            output_params.variable.compare("cons") == 0) {
          xdmf << "    <Attribute Name=\"radiation_press_11\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr11" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_press_12\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr12" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_press_13\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr13" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_press_21\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr21" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_press_22\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr22" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_press_23\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr23" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_press_31\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr31" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_press_32\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr32" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
          xdmf << "    <Attribute Name=\"radiation_press_33\" AttributeType=\"Scalar\" "
          << "Center=\"Cell\">" << std::endl << "      <DataItem Dimensions=\""
          << sdim << "\" NumberType=\"Float\" Precision=\"4\" Format=\"HDF\">"
          << fname << ":/" << bn << "/Pr33" << "</DataItem>" << std::endl
          << "    </Attribute>" << std::endl;
        }
        
      }

      if (output_params.variable.compare("ifov") == 0) {
        for (int n=0; n<(NIFOV); ++n) {
          xdmf << "    <Attribute Name=\"Density\" AttributeType=\"Scalar\" "
               << "Center=\"Cell\">" << std::endl
               << "      <DataItem Dimensions=\"" << sdim << "\" NumberType=\"Float\" "
               << "Precision=\"4\" Format=\"HDF\">" << fname << ":/" << bn << "/ifov"
               << n << "</DataItem>" << std::endl << "    </Attribute>" << std::endl;
        }
      }

      xdmf << "  </Grid>" << std::endl;
    }
    xdmf << "</Grid>" << std::endl << "</Domain>" << std::endl
         << "</Xdmf>" << std::endl;
    xdmf.close();
  }

  return;
}


//--------------------------------------------------------------------------------------
//! \fn void ATHDF5Output::Finalize(ParameterInput *pin)
//  \brief close the file
void ATHDF5Output::Finalize(ParameterInput *pin)
{
  H5Fclose(file);

  delete [] grpid;
  delete [] x1fid;
  delete [] x2fid;
  if(mbsize[2]>1)
    delete [] x3fid;
  delete [] rhoid;
  if (NON_BAROTROPIC_EOS)
    delete [] eid;
  if (RADIATION_ENABLED){
    delete [] erid;
    delete [] er0id;
    delete [] sigmasid;
    delete [] sigmaaid;
    for(int n=0; n<3; n++){
       delete [] frid[n];
       delete [] fr0id[n];
    }
    for(int n=0; n<9; n++)
       delete [] prid[n];
  }
  for(int n=0;n<3;n++) {
    delete [] mid[n];
    if (MAGNETIC_FIELDS_ENABLED)
      delete [] bid[n];
  }
  for(int n=0;n<NIFOV;n++)
    delete [] ifovid[n];
  if (NIFOV > 0)
    delete [] ifovid;

  output_params.file_number++;
  output_params.next_time += output_params.dt;
  pin->SetInteger(output_params.block_name, "file_number", output_params.file_number);
  pin->SetReal(output_params.block_name, "next_time", output_params.next_time);
}
//--------------------------------------------------------------------------------------
//! \fn void ATHDF5Output:::WriteOutputFile(OutputData *pod, MeshBlock *pmb)
//  \brief writes OutputData to file in ATHDF5 format

void ATHDF5Output::WriteOutputFile(OutputData *pod, MeshBlock *pmb)
{
  hid_t did;
  char mbid[8];
  std::string gname="/MeshBlock";
  sprintf(mbid,"%d",pmb->gid);
  gname.append(mbid);
  
  float *data;
  int ndata = std::max(mbsize[0]+1,mbsize[1]+1);
  ndata = std::max(ndata,mbsize[2]+1);
  ndata = std::max(mbsize[0]*mbsize[1]*mbsize[2],ndata);
  data = new float[ndata];

  // coordinates
  for (int i=(pod->data_header.il); i<=(pod->data_header.iu)+1; ++i)
    data[i-(pod->data_header.il)] = (float)(pmb->pcoord->x1f(i));
  H5Dwrite(x1fid[pmb->lid], H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
  H5Dclose(x1fid[pmb->lid]);
  for (int j=(pod->data_header.jl); j<=(pod->data_header.ju)+1; ++j)
    data[j-(pod->data_header.jl)] = (float)(pmb->pcoord->x2f(j));
  H5Dwrite(x2fid[pmb->lid], H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
  H5Dclose(x2fid[pmb->lid]);
  if(dim==3) {
    for (int k=(pod->data_header.kl); k<=(pod->data_header.ku)+1; ++k)
      data[k-(pod->data_header.kl)] = (float)(pmb->pcoord->x3f(k));
    H5Dwrite(x3fid[pmb->lid], H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
    H5Dclose(x3fid[pmb->lid]);
  }
  // data output
  OutputVariable *pvar = pod->pfirst_var;
  int nif=0;
  while (pvar != NULL) {
    int nvar = pvar->data.GetDim4();
    // convert data into float
    for (int n=0; n<nvar; ++n) {
      if(pvar->name.compare("dens")==0 || pvar->name.compare("rho")==0)
        did=rhoid[pmb->lid];
      else if(pvar->name.compare("Etot")==0 || pvar->name.compare("press")==0)
        did=eid[pmb->lid];
      else if(pvar->name.compare("mom")==0 || pvar->name.compare("vel")==0)
        did=mid[n][pmb->lid];
      else if(pvar->name.compare("cc-B")==0)
        did=bid[n][pmb->lid];
      else if(pvar->name.compare("Er")==0)
        did=erid[pmb->lid];
      else if(pvar->name.compare("Er0")==0)
        did=er0id[pmb->lid];
      else if(pvar->name.compare("Sigma_s")==0)
        did=sigmasid[pmb->lid];
      else if(pvar->name.compare("Sigma_a")==0)
        did=sigmaaid[pmb->lid];
      else if(pvar->name.compare("Fr")==0)
        did=frid[n][pmb->lid];
      else if(pvar->name.compare("Fr0")==0)
        did=fr0id[n][pmb->lid];
      else if(pvar->name.compare("Pr")==0)
        did=prid[n][pmb->lid];
      else if(pvar->name.compare("ifov")==0)
        did=ifovid[n][pmb->lid];
      else continue;
      int p=0;
      for (int k=(pod->data_header.kl); k<=(pod->data_header.ku); ++k) {
        for (int j=(pod->data_header.jl); j<=(pod->data_header.ju); ++j) {
          for (int i=(pod->data_header.il); i<=(pod->data_header.iu); ++i) {
            data[p++] = (float)pvar->data(n,k,j,i);
          }
        }
      }
      // write data
      H5Dwrite(did, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
      H5Dclose(did);
    }
    pvar = pvar->pnext;
  }

  H5Gclose(grpid[pmb->lid]);
  
  delete [] data;
  return;
}

#endif
