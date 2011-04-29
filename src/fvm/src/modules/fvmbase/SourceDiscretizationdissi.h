#ifndef _SOURCEDISCRETIZATIONDISSI_H_
#define _SOURCEDISCRETIZATIONDISSI_H_

#include "Field.h"
#include "MultiField.h"
#include "MultiFieldMatrix.h"
#include "Mesh.h"
#include "Discretization.h"
#include "StorageSite.h"
#include "GeomFields.h"
#include "CRConnectivity.h"
#include "CRMatrixRect.h"
#include "Vector.h"
#include "GradientModel.h"


template<class T,class Diag, class OffDiag>
class SourceDiscretizationdissi : public Discretization
{
public:
  typedef Vector<T,3> VectorT3;
  typedef Array<VectorT3> VectorT3Array;
  typedef Gradient<VectorT3> VGradType;
  typedef Array<Gradient<VectorT3> > VGradArray;
  typedef Array<T> TArray;
  typedef CRMatrix<Diag,OffDiag,T> CCMatrix;
  typedef typename CCMatrix::DiagArray DiagArray;
  typedef typename CCMatrix::PairWiseAssembler CCAssembler;


  SourceDiscretizationdissi(const MeshList& meshes,
                            const GeomFields& geomFields,
                            Field& varField,
                            const Field& velocityField,
                            Field& muField,
                            Field& energyField,
                            Field& densityField,
                            Field& sourcedField,
                            Field& sourcecField,
                            Field& sourcepField,
                            const Field& gradientField) :

      Discretization(meshes),
      _geomFields(geomFields),
      _varField(varField),
      _velocityField(velocityField),
      _muField(muField),
      _energyField(energyField),
      _densityField(densityField),
      _sourcedField(sourcedField),
      _sourcecField(sourcecField),
      _sourcepField(sourcepField),

      _gradientField(gradientField)

   {}
  void discretize(const Mesh& mesh, MultiFieldMatrix& mfmatrix,
                  MultiField& xField, MultiField& rField)
  {


    const StorageSite& cells = mesh.getCells();
    const MultiField::ArrayIndex cVarIndex(&_varField,&cells);

    const VGradArray& vGrad =
      dynamic_cast<const VGradArray&>(_gradientField[cells]);

    const TArray& muCell =
      dynamic_cast<const TArray&>(_muField[cells]);

    const TArray& eCell =
      dynamic_cast<const TArray&>(_varField[cells]);

    const TArray& kCell =
      dynamic_cast<const TArray&>(_energyField[cells]);

    const TArray & rhoCell =
      dynamic_cast<const TArray&>(_densityField[cells]);

    TArray& sourceCell =
      dynamic_cast<TArray&>(_sourcedField[cells]);

    TArray& sourcecCell =
      dynamic_cast<TArray&>(_sourcecField[cells]);

    TArray& sourcepCell =
      dynamic_cast<TArray&>(_sourcepField[cells]);




    TArray& rCell = 
      dynamic_cast<TArray&>(rField[cVarIndex]);


    const int nCells = cells.getCount();
    const TArray& cellVolume = dynamic_cast<const TArray&>(_geomFields.volume[cells]);

    CCMatrix& matrix = dynamic_cast<CCMatrix&>(mfmatrix.getMatrix(cVarIndex,cVarIndex));

    DiagArray& diag = matrix.getDiag();
    T x; T y; T dx; T dy; T sum; T ds;
    T onePointfour(1.44);
    T onePointnine(1.92);
    T two(2.0);
    for(int n=0; n<nCells; n++)
    {
        const VGradType& vg = vGrad[n];
        VGradType vgSquare = vGrad[n];
         x = (muCell[n]*eCell[n]*onePointfour)/kCell[n];
         y = (onePointnine*eCell[n]*eCell[n]*rhoCell[n])/kCell[n];
         dx = x/eCell[n];
         dy = (y*two)/eCell[n];
        
        for(int i=0;i<3;i++)
        {
          for(int j=0;j<3;j++)
           {
           vgSquare[i][j] =  vg[i][j]*vg[i][j]+vg[i][j]*vg[j][i] ;
           sum += vgSquare[i][j];

        //     vgSquare[i][j] =  vg[i][j]*vg[i][j] ;
          //   vgSquare[i][j] += vg[i][j]*vg[j][i];
           }
        }
        //cout<<sum;
        sourceCell[n] = sum*x-y ;
        ds = sum*dx-dy;
        sourcecCell[n] = sourceCell[n]- ds*eCell[n];
        sourcepCell[n] = ds;
        rCell[n] +=sourcecCell[n]*cellVolume[n];
        diag[n] -=cellVolume[n]*sourcepCell[n];
    }
}

private:
  const MeshList _meshes;
  const GeomFields& _geomFields;
  Field& _varField;
  const Field& _velocityField;
  Field& _muField;
  Field& _energyField;
  Field& _densityField;
  Field& _sourcedField;
  Field& _sourcecField;
  Field& _sourcepField;
  const Field& _gradientField;

};

#endif
