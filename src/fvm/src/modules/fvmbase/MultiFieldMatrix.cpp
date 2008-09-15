#include "MultiFieldMatrix.h"
#include "CRConnectivity.h"
#include "MultiField.h"
#include "IContainer.h"
#include "StorageSite.h"
#include "OneToOneIndexMap.h"


MultiFieldMatrix::MultiFieldMatrix() :
  _matrices(),
  _coarseSizes(),
  _coarseGhostSizes(),
  _coarseMappers(),
  _coarseSites(),
  _coarseToFineMappings(),
  _coarseConnectivities(),
  _coarseMatrices()
{
  logCtor();
}

MultiFieldMatrix::~MultiFieldMatrix()
{
  logDtor();
}

bool
MultiFieldMatrix::hasMatrix(const Index& row,
                            const Index& col) const 
{
  EntryIndex i(row,col);
  return _matrices.find(i) != _matrices.end();
}

Matrix&
MultiFieldMatrix::getMatrix(const Index& row,
                            const Index& col)
{
  EntryIndex i(row,col);
  return *_matrices.find(i)->second;
}

const Matrix&
MultiFieldMatrix::getMatrix(const Index& row,
                            const Index& col) const
{
  EntryIndex i(row,col);
  return *_matrices.find(i)->second;
}

void
MultiFieldMatrix::initAssembly()
{
  foreach(MatrixMap::value_type& pos, _matrices)
    pos.second->initAssembly();
}

void
MultiFieldMatrix::multiply(IContainer& yB, const IContainer& xB) const
{
  MultiField& y = dynamic_cast<MultiField&>(yB);
  const MultiField& x = dynamic_cast<const MultiField&>(xB);

  const int yLen = y.getLength();
  const int xLen = x.getLength();

  //#pragma omp parallel for
  for(int i=0; i<yLen; i++)
  {
      const Index rowIndex = y.getArrayIndex(i);
      ArrayBase& yI = y[rowIndex];
      yI.zero();
      for(int j=0; j<xLen; j++)
      {
          const Index colIndex = x.getArrayIndex(j);
          if (hasMatrix(rowIndex,colIndex))
          {
              const Matrix& mIJ = getMatrix(rowIndex,colIndex);
              mIJ.multiplyAndAdd(yI,x[colIndex]);
          }
      }
  }

}

void
MultiFieldMatrix::multiplyAndAdd(IContainer& yB, const IContainer& xB) const
{
  MultiField& y = dynamic_cast<MultiField&>(yB);
  const MultiField& x = dynamic_cast<const MultiField&>(xB);

  const int yLen = y.getLength();
  const int xLen = x.getLength();

  //#pragma omp parallel for
  for(int i=0; i<yLen; i++)
  {
      const Index rowIndex = y.getArrayIndex(i);
      ArrayBase& yI = y[rowIndex];
      for(int j=0; j<xLen; j++)
      {
          const Index colIndex = x.getArrayIndex(j);
          if (hasMatrix(rowIndex,colIndex))
          {
              const Matrix& mIJ = getMatrix(rowIndex,colIndex);
              mIJ.multiplyAndAdd(yI,x[colIndex]);
          }
      }
  }
}

void 
MultiFieldMatrix::forwardGS(IContainer& xB, const IContainer& bB, IContainer& tempB) const
{
  MultiField& x = dynamic_cast<MultiField&>(xB);
  const MultiField& b = dynamic_cast<const MultiField&>(bB);
   MultiField& temp = dynamic_cast<MultiField&>(tempB);

  const int xLen = x.getLength();


  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = x.getArrayIndex(i);
      if (hasMatrix(rowIndex,rowIndex))
      {
          const ArrayBase& bI = b[rowIndex];
          ArrayBase& r = temp[rowIndex];
          const StorageSite& rowSite = *rowIndex.second;
          r.copyPartial(bI,0,rowSite.getSelfCount());
          
          for(int j=0; j<xLen; j++)
          {
              const Index colIndex = x.getArrayIndex(j);
              if ((rowIndex!=colIndex) && hasMatrix(rowIndex,colIndex))
              {
                  const Matrix& mIJ = getMatrix(rowIndex,colIndex);
                  mIJ.multiplyAndAdd(r,x[colIndex]);
              }
          }
          
          const Matrix& mII = getMatrix(rowIndex,rowIndex);
          x.syncLocal(rowIndex);
          mII.forwardGS(x[rowIndex],r,r);
      }
  }
}

void 
MultiFieldMatrix::solveBoundary(IContainer& xB, const IContainer& bB, IContainer& tempB) const
{
  MultiField& x = dynamic_cast<MultiField&>(xB);
  const MultiField& b = dynamic_cast<const MultiField&>(bB);
   MultiField& temp = dynamic_cast<MultiField&>(tempB);

  const int xLen = x.getLength();

  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = x.getArrayIndex(i);
      if (hasMatrix(rowIndex,rowIndex))
      {
          const ArrayBase& bI = b[rowIndex];
          ArrayBase& r = temp[rowIndex];
          r.copyFrom(bI);
          
          for(int j=0; j<xLen; j++)
          {
              const Index colIndex = x.getArrayIndex(j);
              if ((rowIndex!=colIndex) && hasMatrix(rowIndex,colIndex))
              {
                  const Matrix& mIJ = getMatrix(rowIndex,colIndex);
                  mIJ.multiplyAndAdd(r,x[colIndex]);
              }
          }
          
          const Matrix& mII = getMatrix(rowIndex,rowIndex);
          mII.solveBoundary(x[rowIndex],r,r);
      }
  }
}



void
MultiFieldMatrix::reverseGS(IContainer& xB, const IContainer& bB, IContainer& tempB) const
{
  MultiField& x = dynamic_cast<MultiField&>(xB);
  const MultiField& b = dynamic_cast<const MultiField&>(bB);
  MultiField& temp = dynamic_cast<MultiField&>(tempB);

  const int xLen = x.getLength();

  for(int i=xLen-1; i>=0; i--)
  {
      const Index rowIndex = x.getArrayIndex(i);
      if (hasMatrix(rowIndex,rowIndex))
      {
          const ArrayBase& bI = b[rowIndex];
          ArrayBase& r = temp[rowIndex];
          const StorageSite& rowSite = *rowIndex.second;
          r.copyPartial(bI,0,rowSite.getSelfCount());
                    
          for(int j=0; j<xLen; j++)
          {
              const Index colIndex = x.getArrayIndex(j);
              if ((rowIndex!=colIndex) && hasMatrix(rowIndex,colIndex))
              {
                  const Matrix& mIJ = getMatrix(rowIndex,colIndex);
                  mIJ.multiplyAndAdd(r,x[colIndex]);
              }
          }
          
          const Matrix& mII = getMatrix(rowIndex,rowIndex);
          x.syncLocal(rowIndex);
          mII.reverseGS(x[rowIndex],r,r);
      }
  }
}

void
MultiFieldMatrix::computeResidual(const IContainer& xB, const IContainer& bB,
                                  IContainer& rB) const
{
  const MultiField& x = dynamic_cast<const MultiField&>(xB);
  const MultiField& b = dynamic_cast<const MultiField&>(bB);
  MultiField& r = dynamic_cast<MultiField&>(rB);

  const int xLen = x.getLength();

  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = x.getArrayIndex(i);
      if (hasMatrix(rowIndex,rowIndex))
      {
          const ArrayBase& bI = b[rowIndex];
          const StorageSite& rowSite = *rowIndex.second;
          ArrayBase& rI = r[rowIndex];
          rI.copyPartial(bI,0,rowSite.getSelfCount());
          rI.zeroPartial(rowSite.getSelfCount(),rowSite.getCount());
          
          for(int j=0; j<xLen; j++)
          {
              const Index colIndex = x.getArrayIndex(j);
              if (hasMatrix(rowIndex,colIndex))
              {
                  const Matrix& mIJ = getMatrix(rowIndex,colIndex);
                  mIJ.multiplyAndAdd(rI,x[colIndex]);
              }
          }
      }
  }
}

void
MultiFieldMatrix::removeMatrix(const Index& row, const Index& col)
{
  EntryIndex e(row,col);

  _matrices.erase(e);
}


void 
MultiFieldMatrix::createCoarsening(MultiField& coarseIndex,
                                    const int groupSize,
                                    const double weightRatioThreshold)
{

  const int xLen = coarseIndex.getLength();


  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = coarseIndex.getArrayIndex(i);
      if (hasMatrix(rowIndex,rowIndex))
      {
          Matrix& mII = getMatrix(rowIndex,rowIndex);
          _coarseSizes[rowIndex] =
            mII.createCoarsening(coarseIndex[rowIndex],
                                 groupSize,weightRatioThreshold);
      }
  }
}



void
MultiFieldMatrix::syncGhostCoarsening(MultiField& coarseIndexField)
{
  const int xLen = coarseIndexField.getLength();

  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = coarseIndexField.getArrayIndex(i);

      Array<int>&  coarseIndex = dynamic_cast<Array<int>& >(coarseIndexField[rowIndex]);

      int coarseGhostSize=0;
      const int coarseSize = _coarseSizes.find(rowIndex)->second;
      
      const StorageSite& site = *rowIndex.second;

      const StorageSite::MappersMap& mappers = site.getMappers();
      
      for(StorageSite::MappersMap::const_iterator pos = mappers.begin();
          pos != mappers.end();
          ++pos)
        {
          const StorageSite& oSite = *pos->first;
          const Array<int>& toIndices = pos->second->toIndices;

          map<int,int> otherToMyMapping;

          const int nGhostRows = toIndices.getLength();
          for(int ng=0; ng<nGhostRows; ng++)
          {
              const int fineIndex = toIndices[ng];
              const int coarseOtherIndex = coarseIndex[fineIndex];

              if (otherToMyMapping.find(coarseOtherIndex) !=
                  otherToMyMapping.end())
              {
                  coarseIndex[fineIndex] = otherToMyMapping[coarseOtherIndex];
              }
              else
              {
                  coarseIndex[fineIndex] = coarseGhostSize+coarseSize;
                  otherToMyMapping[coarseOtherIndex] = coarseIndex[fineIndex];
                  coarseGhostSize++;
              }

          }

          const int coarseMappersSize = otherToMyMapping.size();
          Array<int> * coarseToIndices = new Array<int>(coarseMappersSize);
          Array<int> * coarseFromIndices = new Array<int>(coarseMappersSize);
          
          int ncm=0;
          for(map<int,int>::const_iterator pos =otherToMyMapping.begin();
              pos!=otherToMyMapping.end(); ++pos)
          {
              (*coarseToIndices)[ncm] = pos->second;
              (*coarseFromIndices)[ncm] = pos->first;
              ncm++;
          }

          Index colIndex(rowIndex.first,&oSite);
          EntryIndex e(rowIndex,colIndex);

          shared_ptr<OneToOneIndexMap> coarseMap(new OneToOneIndexMap(coarseFromIndices,
                                                                      coarseToIndices));
          _coarseMappers[e]=coarseMap;
        }
      _coarseGhostSizes[rowIndex]=coarseGhostSize;
  }
}

void
MultiFieldMatrix::createCoarseToFineMapping(const MultiField& coarseIndexField)
{
  const int xLen = coarseIndexField.getLength();
  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = coarseIndexField.getArrayIndex(i);

      const StorageSite& fineSite = *rowIndex.second;
      
      const StorageSite& coarseSite = *_coarseSites[rowIndex];

      const int nFineRows = fineSite.getCount();

      const Array<int>& coarseIndex =
        dynamic_cast<const Array<int>& >(coarseIndexField[rowIndex]);

      
      shared_ptr<CRConnectivity> coarseToFine(new CRConnectivity(coarseSite,fineSite));

      coarseToFine->initCount();
      
      for(int nr=0; nr<nFineRows; nr++)
        if (coarseIndex[nr]>=0)
          coarseToFine->addCount(coarseIndex[nr],1);
      
      coarseToFine->finishCount();

      for(int nr=0; nr<nFineRows; nr++)
        if (coarseIndex[nr]>=0)
          coarseToFine->add(coarseIndex[nr],nr);

      coarseToFine->finishAdd();
      _coarseToFineMappings[rowIndex]=coarseToFine;
  }
}

void
MultiFieldMatrix::createCoarseConnectivity(MultiField& coarseIndex)
{

  const int xLen = coarseIndex.getLength();

  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = coarseIndex.getArrayIndex(i);

      const StorageSite& coarseRowSite = *_coarseSites[rowIndex];

      const CRConnectivity& coarseToFine = *_coarseToFineMappings[rowIndex];

      for(int j=0; j<xLen; j++)
      {
          const Index colIndex = coarseIndex.getArrayIndex(j);
          if (hasMatrix(rowIndex,colIndex))
          {
              Matrix& mIJ = getMatrix(rowIndex,colIndex);

              const StorageSite& coarseColSite = *_coarseSites[colIndex];

              shared_ptr<CRConnectivity> coarseConnectivity
                (mIJ.createCoarseConnectivity(coarseIndex[rowIndex],
                                              coarseToFine,
                                              coarseRowSite,coarseColSite));
              EntryIndex e(rowIndex,colIndex);
              _coarseConnectivities[e]=coarseConnectivity;
          }
      }
  }
}

void
MultiFieldMatrix::createCoarseMatrices(MultiField& coarseIndex)
{
  const int xLen = coarseIndex.getLength();
  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = coarseIndex.getArrayIndex(i);

      const CRConnectivity& coarseToFine = *_coarseToFineMappings[rowIndex];

      for(int j=0; j<xLen; j++)
      {
          const Index colIndex = coarseIndex.getArrayIndex(j);
          if (hasMatrix(rowIndex,colIndex))
          {
              Matrix& mIJ = getMatrix(rowIndex,colIndex);
              EntryIndex e(rowIndex,colIndex);

              const CRConnectivity& coarseConnectivity = *_coarseConnectivities[e];

              shared_ptr<Matrix> coarseMatrix 
                (mIJ.createCoarseMatrix(coarseIndex[rowIndex],
                                        coarseToFine,
                                        coarseConnectivity));
              _coarseMatrices[e]=coarseMatrix;
          }
      }
  }
}


void
MultiFieldMatrix::injectResidual(const MultiField& coarseIndex,
                                 const MultiField& fineResidualField,
                                 MultiField& coarseBField)
{
  const int xLen = fineResidualField.getLength();

  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = fineResidualField.getArrayIndex(i);

      const StorageSite& coarseRowSite = *_coarseSites[rowIndex];

      const Index coarseRowIndex(rowIndex.first,&coarseRowSite);
      
      const ArrayBase& fineResidual =
        dynamic_cast<const ArrayBase&>(fineResidualField[rowIndex]);

      const ArrayBase& fineToCoarse =
        dynamic_cast<const ArrayBase&>(coarseIndex[rowIndex]);

      ArrayBase& coarseB =  dynamic_cast<ArrayBase&>(coarseBField[coarseRowIndex]);

      fineResidual.inject(coarseB,fineToCoarse,rowIndex.second->getSelfCount());
  }
}

void
MultiFieldMatrix::correctSolution(const MultiField& coarseIndex,
                                  MultiField& fineSolutionField,
                                  const MultiField& coarseSolutionField)
{
  const int xLen = fineSolutionField.getLength();

  //#pragma omp parallel for
  for(int i=0; i<xLen; i++)
  {
      const Index rowIndex = fineSolutionField.getArrayIndex(i);

      const StorageSite& coarseRowSite = *_coarseSites[rowIndex];

      const Index coarseRowIndex(rowIndex.first,&coarseRowSite);
      
      ArrayBase& fineSolution = dynamic_cast<ArrayBase&>(fineSolutionField[rowIndex]);
      fineSolution.correct(coarseSolutionField[coarseRowIndex],
                           coarseIndex[rowIndex],
                           rowIndex.second->getSelfCount());
  }
}


int
MultiFieldMatrix::getSize() const
{
  int size=0;
  foreach(const MatrixMap::value_type& pos, _matrices)
  {
      EntryIndex k = pos.first;

      if (k.first == k.second)
      {
          size += k.first.second->getCount();
      }
  }
  return size;
}
