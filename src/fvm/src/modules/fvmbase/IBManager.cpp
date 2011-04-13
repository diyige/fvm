#include "IBManager.h"
#include "AABB.h"
#include "KSearchTree.h"
#include "Mesh.h"
#include "GradientModel.h"
#include <stack>

#ifdef FVM_PARALLEL
#include <mpi.h>
#endif



IBManager::IBManager(GeomFields& geomFields,
                     Mesh& solidBoundaryMesh,
                     const MeshList& fluidMeshes):
  fluidNeighborsPerIBFace(8),
  fluidNeighborsPerSolidFace(10),
  solidNeighborsPerIBFace(3),
  _geomFields(geomFields),
  _solidBoundaryMesh(solidBoundaryMesh),
  _fluidMeshes(fluidMeshes)
{}

void IBManager::update()
{
  AABB sMeshesAABB(_solidBoundaryMesh);

  const StorageSite& solidMeshFaces = _solidBoundaryMesh.getFaces();
  
  const Vec3DArray& solidMeshCoords =
    dynamic_cast<const Vec3DArray&>(_geomFields.coordinate[solidMeshFaces]);

  KSearchTree solidMeshKSearchTree(solidMeshCoords);

  const int numFluidMeshes = _fluidMeshes.size();
  
  for (int n=0; n<numFluidMeshes; n++)
  {
      Mesh& fluidMesh = *_fluidMeshes[n];

      markIntersections(fluidMesh, sMeshesAABB);
  }


  int nIter=0;
  int nFound=0;


  // repeat till we find no more fluid cells
  do
  {
      nFound = 0;
      _geomFields.ibType.syncLocal();
      
      for (int n=0; n<numFluidMeshes; n++)
      {
          Mesh& fluidMesh = *_fluidMeshes[n];
          
          nFound += markFluid(fluidMesh);
      }
#ifdef FVM_PARALLEL
     MPI::COMM_WORLD.Allreduce( MPI::IN_PLACE, &nFound, 1, MPI::INT, MPI::SUM );
     if ( MPI::COMM_WORLD.Get_rank() == 0 ) 
        cout << "iteration " << nIter << ": found " << nFound << " fluid cells " << endl;
#endif

#ifndef FVM_PARALLEL
      cout << "iteration " << nIter << ": found " << nFound << " fluid cells " << endl;
#endif

      nIter++;
  } 
  while(nFound > 0);

  nFound=0;
  for (int n=0; n<numFluidMeshes; n++)
  {
      Mesh& fluidMesh = *_fluidMeshes[n];

      nFound += markSolid(fluidMesh);
  }

      
  for (int n=0; n<numFluidMeshes; n++)
  {
      Mesh& fluidMesh = *_fluidMeshes[n];

      markIBTypePlus(fluidMesh);
  }

  for (int n=0; n<numFluidMeshes; n++)
  {
      Mesh& fluidMesh = *_fluidMeshes[n];

      createIBFaces(fluidMesh);
  }

  vector<NearestCell> solidFacesNearestCell(solidMeshFaces.getCount());
  
  for (int n=0; n<numFluidMeshes; n++)
  {
      Mesh& fluidMesh = *_fluidMeshes[n];

      const StorageSite& cells = fluidMesh.getCells();
      const int numCells = cells.getSelfCount();
      
      IntArray& cellIBType = dynamic_cast<IntArray&>(_geomFields.ibType[cells]);

      const Vec3DArray& cellCoords =
        dynamic_cast<const Vec3DArray&>(_geomFields.coordinate[cells]);
      
      KSearchTree fluidCellsTree;

      for(int c=0; c<numCells; c++)
      {
          if (cellIBType[c] == Mesh::IBTYPE_FLUID)
            fluidCellsTree.insert(cellCoords[c],c);
      }

      createIBInterpolationStencil(fluidMesh,fluidCellsTree,solidMeshKSearchTree);
      findNearestCellForSolidFaces(fluidMesh,fluidCellsTree,solidFacesNearestCell);
  }


#ifdef FVM_PARALLEL
    vector<doubleIntStruct>  solidFacesNearestCellMPI(solidMeshFaces.getCount());
    const int procID = MPI::COMM_WORLD.Get_rank();
    const int faceCount = solidMeshFaces.getCount();
    for( int i = 0; i < faceCount; i++ ){
       const Mesh& mesh = *solidFacesNearestCell[i].mesh;
       const int meshID = mesh.getID();
       const int tag = (std::max(procID, meshID) << 16 ) | ( std::min(procID,meshID) );
       const double val = solidFacesNearestCell[i].distanceSquared;
       solidFacesNearestCellMPI[i].VALUE = val;
       solidFacesNearestCellMPI[i].TAG   = tag;
    } 
    //mpi comminuction
    MPI::COMM_WORLD.Allreduce(MPI::IN_PLACE, &solidFacesNearestCellMPI[0], faceCount, MPI::DOUBLE_INT, MPI::MINLOC);
    //now update solidFAcesNearestCell
    for ( int i = 0; i < faceCount; i++ ){
       const Mesh* meshThis = solidFacesNearestCell[i].mesh;
       const int meshIDThis = meshThis->getID();
       const int tagThis = (std::max(procID, meshIDThis) << 16 ) | ( std::min(procID,meshIDThis) );
       if ( tagThis != solidFacesNearestCellMPI[i].TAG ){
            solidFacesNearestCell[i].mesh = NULL;
       }
    }
#endif

  for (int n=0; n<numFluidMeshes; n++)
  {
      Mesh& fluidMesh = *_fluidMeshes[n];
      createSolidInterpolationStencil(fluidMesh,solidFacesNearestCell);
  }
}



void
IBManager::markIntersections(Mesh& fluidMesh, AABB& sMeshesAABB)
{
  const Array<Vector<double,3> >& meshCoords = fluidMesh.getNodeCoordinates();
  const StorageSite& faces = fluidMesh.getFaces();
  const StorageSite& cells = fluidMesh.getCells();
  
  const CRConnectivity& faceCells = fluidMesh.getAllFaceCells();
  const CRConnectivity& cellNodes = fluidMesh.getCellNodes();

  IntArray& cellIBType = dynamic_cast<IntArray&>(_geomFields.ibType[cells]);

  cellIBType = Mesh::IBTYPE_UNKNOWN;
  
  const int nFaces = faces.getCount();

  int nIntersections = 0;

  const bool is2D = fluidMesh.getDimension() == 2;

  if (is2D)
  {
      const int nCells = cells.getSelfCount();
      for(int n=0; n<nCells; n++)
      {
          const Vec3D& a = meshCoords[cellNodes(n,0)];
          const Vec3D& b = meshCoords[cellNodes(n,1)];
          const Vec3D& c = meshCoords[cellNodes(n,2)];
          
          if (sMeshesAABB.hasIntersectionWithTriangle(a,b,c))
          {
              cellIBType[n] = Mesh::IBTYPE_BOUNDARY;
              
              nIntersections++;
          }
          else if (cellNodes.getCount(n) == 4)
          {
              
              const Vec3D& d = meshCoords[cellNodes(n,3)];
              if (sMeshesAABB.hasIntersectionWithTriangle(c,d,a))
              {
                  cellIBType[n] = Mesh::IBTYPE_BOUNDARY;
                  nIntersections++;
              }
          }
      }
  }
  else
  {
      const CRConnectivity& faceNodes = fluidMesh.getAllFaceNodes();
      // loop through all the faces to find cells that intersect the AABB
      // faces and mark them to be of type boundary
      
      for(int f=0; f<nFaces; f++)
      {
          const int c0 = faceCells(f,0);
          const int c1 = faceCells(f,1);
          
          const Vec3D& a = meshCoords[faceNodes(f,0)];
          const Vec3D& b = meshCoords[faceNodes(f,1)];
          const Vec3D& c = meshCoords[faceNodes(f,2)];
          
          // check the triangle formed by the first three vertices
          if (sMeshesAABB.hasIntersectionWithTriangle(a,b,c))
          {
              cellIBType[c0] = Mesh::IBTYPE_BOUNDARY;
              cellIBType[c1] = Mesh::IBTYPE_BOUNDARY;
              
              nIntersections++;
          }
          else if (faceNodes.getCount(f) == 4)
          {
              // check the other triangle if this is a quad face
              const Vec3D& d = meshCoords[faceNodes(f,3)];
              if (sMeshesAABB.hasIntersectionWithTriangle(c,d,a))
              {
                  cellIBType[c0] = Mesh::IBTYPE_BOUNDARY;
                  cellIBType[c1] = Mesh::IBTYPE_BOUNDARY;
                  nIntersections++;
              }
          }
      }
  }


  // mark all cells adjacent to the boundaries as fluid unless they
  // have been found to be IBTYPE_BOUNDARY in the loop above
  
  foreach(const FaceGroupPtr fgPtr, fluidMesh.getBoundaryFaceGroups())
  {
      const FaceGroup& fg = *fgPtr;
      const StorageSite& faces = fg.site;
      
      const CRConnectivity& faceCells = fluidMesh.getFaceCells(faces);
      const int nFaces = faces.getCount();
      for(int f=0; f<nFaces; f++)
      {
          const int c0 = faceCells(f,0);
          const int c1 = faceCells(f,1);
          
          if ((cellIBType[c0] == Mesh::IBTYPE_UNKNOWN) &&
              (cellIBType[c1] == Mesh::IBTYPE_UNKNOWN))
          {
              cellIBType[c1] = Mesh::IBTYPE_FLUID;
              cellIBType[c0] = Mesh::IBTYPE_FLUID;
          }
          else
          {
              cellIBType[c1] = Mesh::IBTYPE_BOUNDARY;
          }
      }
  }
  
}


int
IBManager::markFluid(Mesh& fluidMesh)
{

  const StorageSite& cells = fluidMesh.getCells();

  IntArray& cellIBType = dynamic_cast<IntArray&>(_geomFields.ibType[cells]);


  const int nCellsTotal = cells.getCount();

  int nFound=0;

 
  const CRConnectivity& cellCells = fluidMesh.getCellCells();


  // loop over all cells including externals and if they are of type
  // fluid mark any other cells that are unknown but can be reached
  // from there
  
  for(int c=0; c<nCellsTotal; c++)
  {
      if (cellIBType[c] == Mesh::IBTYPE_FLUID)
      {
          
          stack<int> cellsToCheck;
          cellsToCheck.push(c);
          
           
          while(!cellsToCheck.empty())
          {
              int c_nb = cellsToCheck.top();
              cellsToCheck.pop();
              const int nNeighbors = cellCells.getCount(c_nb);
              for(int nn=0; nn<nNeighbors; nn++)
              {
                  const int neighbor = cellCells(c_nb,nn);
                  if (cellIBType[neighbor] == Mesh::IBTYPE_UNKNOWN)
                  {
                      cellIBType[neighbor] = Mesh::IBTYPE_FLUID;
                      nFound++;
                      cellsToCheck.push(neighbor);
                  }
              }
              
          }
      }
  }

  return nFound;
}

int
IBManager::markSolid(Mesh& fluidMesh)
{

  const StorageSite& cells = fluidMesh.getCells();

  IntArray& cellIBType = dynamic_cast<IntArray&>(_geomFields.ibType[cells]);


  const int nCellsTotal = cells.getCount();

  int nFound=0;

  //everything that is not marked is solid  
  for(int c=0; c<nCellsTotal; c++)
  {
      if (cellIBType[c] == Mesh::IBTYPE_UNKNOWN)
      {
          cellIBType[c] = Mesh::IBTYPE_SOLID;
          nFound++;
      }
  }

  return nFound;
}


void
IBManager::markIBTypePlus(Mesh& fluidMesh)
{
  int nFluid=0;
  int nSolid=0;
  int nBoundary=0;
  StorageSite& cells = fluidMesh.getCells();
  const int nCellsTotal = cells.getCount();
  const IntArray& cellIBType = dynamic_cast<IntArray&>(_geomFields.ibType[cells]);

  for(int c=0; c<nCellsTotal; c++)
  {
      if (cellIBType[c] == Mesh::IBTYPE_FLUID)
        nFluid++;
      else if (cellIBType[c] == Mesh::IBTYPE_SOLID)
        nSolid++;
      else if (cellIBType[c] == Mesh::IBTYPE_BOUNDARY)
        nBoundary++;
      else{
	//throw CException("invalid ib type");
	}	
  }

  cout << " found " << nFluid << " fluid, "
       << nSolid << " solid and "
       << nBoundary << " boundary cells " << endl;

}
void
IBManager::createIBFaces(Mesh& fluidMesh)
{
  const StorageSite& faces = fluidMesh.getFaces();
  const StorageSite& cells = fluidMesh.getCells();
  
  const CRConnectivity& faceCells = fluidMesh.getAllFaceCells();

  IntArray& cellIBType = dynamic_cast<IntArray&>(_geomFields.ibType[cells]);

  const int nFaces = faces.getCount();

  // find number of IBFaces

  int nIBFaces=0;
  for(int f=0; f<nFaces; f++)
  {
      const int c0 = faceCells(f,0);
      const int c1 = faceCells(f,1);

      const int ibType0 = cellIBType[c0];
      const int ibType1 = cellIBType[c1];

      if ((ibType0 == Mesh::IBTYPE_FLUID && ibType1 == Mesh::IBTYPE_BOUNDARY) ||
          (ibType1 == Mesh::IBTYPE_FLUID && ibType0 == Mesh::IBTYPE_BOUNDARY))
      {
          nIBFaces++;
      }

      if ((ibType0 == Mesh::IBTYPE_FLUID && ibType1 == Mesh::IBTYPE_SOLID) ||
          (ibType1 == Mesh::IBTYPE_FLUID && ibType0 == Mesh::IBTYPE_SOLID))
      {
          throw CException("found face between solid and fluid cells");
      }

  }

  StorageSite& ibFaces = fluidMesh.getIBFaces();
  ibFaces.setCount(nIBFaces);

  shared_ptr<IntArray > ibFaceListPtr(new IntArray(nIBFaces));
  
  IntArray& ibFaceList = *ibFaceListPtr;

  nIBFaces = 0;
  for(int f=0; f<nFaces; f++)
  {
      const int c0 = faceCells(f,0);
      const int c1 = faceCells(f,1);

      const int ibType0 = cellIBType[c0];
      const int ibType1 = cellIBType[c1];

      if ((ibType0 == Mesh::IBTYPE_FLUID && ibType1 == Mesh::IBTYPE_BOUNDARY) ||
          (ibType1 == Mesh::IBTYPE_FLUID && ibType0 == Mesh::IBTYPE_BOUNDARY))
      {
          ibFaceList[nIBFaces++] = f;
      }
  }
  
  fluidMesh.setIBFaces(ibFaceListPtr);
  cout << " found " << nIBFaces << " ib Faces " << endl;

  GradientModelBase::clearGradientMatrix(fluidMesh);
}

/**
 * given a set of cells, add all their fluid ibtype neighbors to the
 * list if they aren't already in it.
 * 
 */

void addFluidNeighbors(set<int>& neighbors,
                       const CRConnectivity& cellCells,
                       const Array<int>& ibType)
{
  set<int> newNeighbors;
  foreach(int c, neighbors)
  {
      const int neighborCount = cellCells.getCount(c);
      for(int nnb=0; nnb<neighborCount; nnb++)
      {
          const int c_nb = cellCells(c,nnb);
          if (ibType[c_nb] == Mesh::IBTYPE_FLUID)
            newNeighbors.insert(c_nb);
      }
  }
  neighbors.insert(newNeighbors.begin(),newNeighbors.end());
}

void
IBManager::createIBInterpolationStencil(Mesh& mesh,
                                         KSearchTree& fluidCellsTree,
                                         KSearchTree& solidFacesTree)
{
  const StorageSite& cells = mesh.getCells();
  const StorageSite& solidMeshFaces = _solidBoundaryMesh.getFaces();
  StorageSite& ibFaces = mesh.getIBFaces();
  const int nIBFaces = ibFaces.getCount();

  const Vec3DArray& faceCentroid =
    dynamic_cast<const Vec3DArray&>(_geomFields.coordinate[mesh.getFaces()]);

  Array<int> fluidNeighbors(1);
  Array<int> solidNeighbors(solidNeighborsPerIBFace);

  const Array<int>& ibFaceIndices = mesh.getIBFaceList();

  shared_ptr<CRConnectivity> ibFaceToCells
    (new CRConnectivity(ibFaces,cells));
  
  shared_ptr<CRConnectivity> ibFaceToSolid
    (new CRConnectivity(ibFaces,solidMeshFaces));

  const CRConnectivity& cellCells = mesh.getCellCells();
  IntArray& cellIBType = dynamic_cast<IntArray&>(_geomFields.ibType[cells]);


  vector<NearestCell> nearestCellForIBFace(nIBFaces);

  ibFaceToCells->initCount();
  ibFaceToSolid->initCount();

  for(int f=0; f<nIBFaces; f++)
  {
      ibFaceToSolid->addCount(f,solidNeighborsPerIBFace);
  }

  ibFaceToSolid->finishCount();

  // in first pass we find the number of fluid cells and also set all the solid neighbors
  for(int f=0; f<nIBFaces; f++)
  {
      // the index of this ib face in the mesh faces
      const int gf = ibFaceIndices[f];
      const Vec3D& xf = faceCentroid[gf];

      // find the closest fluid cell
      fluidCellsTree.findNeighbors(xf, 1, fluidNeighbors);

      NearestCell& nc = nearestCellForIBFace[f];

      nc.neighbors.insert(fluidNeighbors[0]);

      int nLayers=0;
      // repeat till we have the required number but also protect
      // against infinite loop by capping the max number of layers
      while( ((int)nc.neighbors.size() < fluidNeighborsPerIBFace) &&
             (nLayers < 10))
      {
          addFluidNeighbors(nc.neighbors,cellCells,cellIBType);
          nLayers++;
      }
      if (nLayers == 10) 
        throw CException("not enough fluid cells for IB face interpolation");

      ibFaceToCells->addCount(f,nc.neighbors.size());

      // locate the required number of solid faces
      solidFacesTree.findNeighbors(xf, solidNeighborsPerIBFace, solidNeighbors);

      for(int n=0; n<solidNeighborsPerIBFace; n++)
        ibFaceToSolid->add(f,solidNeighbors[n]);
  }

  ibFaceToCells->finishCount();
  ibFaceToSolid->finishAdd();

  for(int f=0; f<nIBFaces; f++)
  {
      NearestCell& nc = nearestCellForIBFace[f];
      foreach(int nb, nc.neighbors)
      {
          ibFaceToCells->add(f,nb);
      }
  }
  
  ibFaceToCells->finishAdd();

  mesh.setConnectivity(ibFaces,cells,ibFaceToCells);
  mesh.setConnectivity(ibFaces,solidMeshFaces,ibFaceToSolid);
  
}

void
IBManager::findNearestCellForSolidFaces(Mesh& mesh,
                                        KSearchTree& fluidCellsTree,
                                        vector<NearestCell>& nearest)
                                           
{
  const StorageSite& cells = mesh.getCells();
  const StorageSite& solidMeshFaces = _solidBoundaryMesh.getFaces();

  const int nSolidFaces = solidMeshFaces.getCount();
  
  const Vec3DArray& cellCentroid =
    dynamic_cast<const Vec3DArray&>(_geomFields.coordinate[cells]);

  const Vec3DArray& solidFaceCentroid =
    dynamic_cast<const Vec3DArray&>(_geomFields.coordinate[solidMeshFaces]);


  Array<int> fluidNeighbors(1);

  for(int f=0; f<nSolidFaces; f++)
  {
      const Vec3D& xf = solidFaceCentroid[f];
      fluidCellsTree.findNeighbors(xf, 1, fluidNeighbors);

      const int c = fluidNeighbors[0];
      const Vec3D& xc = cellCentroid[c];
      const double distanceSquared = mag2(xf-xc);
      NearestCell& nc = nearest[f];
      if ((nc.mesh == 0) || (nc.distanceSquared > distanceSquared))
      {
          nc.mesh = &mesh;
          nc.cell = c;
          nc.distanceSquared = distanceSquared;
      }
  }
}


void
IBManager::createSolidInterpolationStencil(Mesh& mesh,
                                           vector<NearestCell>& nearest)
                                           
{
  const StorageSite& cells = mesh.getCells();
  const StorageSite& solidMeshFaces = _solidBoundaryMesh.getFaces();

  const int nSolidFaces = solidMeshFaces.getCount();
  IntArray& cellIBType = dynamic_cast<IntArray&>(_geomFields.ibType[cells]);
  const CRConnectivity& cellCells = mesh.getCellCells();
  

  shared_ptr<CRConnectivity> solidFacesToCells
    (new CRConnectivity(solidMeshFaces,cells));
  
  solidFacesToCells->initCount();

  for(int f=0; f<nSolidFaces; f++)
  {
      NearestCell& nc = nearest[f];
      if (nc.mesh == &mesh)
      {
          const int c = nc.cell;
          nc.neighbors.insert(c);
          
          int nLayers=0;

          // repeat till we have the required number but also protect
          // against infinite loop by capping the max number of layers
          while( ((int)nc.neighbors.size() < fluidNeighborsPerSolidFace) &&
                 (nLayers < 10))
          {
              addFluidNeighbors(nc.neighbors,cellCells,cellIBType);
              nLayers++;
          }
          //if (nLayers == 10)
          //  throw CException("not enough fluid cells for solid face interpolation");
          solidFacesToCells->addCount(f,nc.neighbors.size());
      }
  }
  
  solidFacesToCells->finishCount();

  for(int f=0; f<nSolidFaces; f++)
  {
      NearestCell& nc = nearest[f];
      if (nc.mesh == &mesh)
      {
          foreach(int nb, nc.neighbors)
          {
              solidFacesToCells->add(f,nb);
          }
      }
  }
  
  solidFacesToCells->finishAdd();
  mesh.setConnectivity(solidMeshFaces,cells,solidFacesToCells);

}
