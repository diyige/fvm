#ifndef _KINETICBOUNDARYCONDITIONS_H_
#define _KINETICBOUNDARYCONDITIONS_H_
#include <stdio.h>
#include "Mesh.h"
#include <vector>
#include <cmath>
#include "NumType.h"
#include "Array.h"
#include "Vector.h"
#include "Field.h"
#include "StorageSite.h"
#include "CRConnectivity.h"
#include "MultiFieldMatrix.h"
#include "CRMatrix.h"
#include "FluxJacobianMatrix.h"
#include "DiagonalMatrix.h"

template<class X,class Diag,class OffDiag>
class KineticBoundaryConditions
{
 public :
  
  typedef typename NumTypeTraits<X>::T_Scalar T_Scalar;

  typedef Array<int> IntArray;
  
  typedef Array<T_Scalar> TArray;
  typedef Vector<T_Scalar,3> VectorT3;  
  typedef Array<VectorT3> VectorT3Array;

  typedef Array<X> XArray;
  typedef Vector<X,3> VectorX3; //new for esbgk
  typedef Array<VectorX3> VectorX3Array;
  
 

  KineticBoundaryConditions(const StorageSite& faces,
			    const Mesh& mesh,
			    const GeomFields& geomFields,
			    const Quadrature<X>& quadrature,
			    MacroFields& macroFields,
			    DistFunctFields<X>& dsfPtr):
			  
    _faces(faces),
    _cells(mesh.getCells()),
    _ibType(dynamic_cast<const IntArray&>(geomFields.ibType[_cells])),
    _quadrature(quadrature), 
    _macroFields(macroFields),
    _dsfPtr(dsfPtr),
    _faceCells(mesh.getFaceCells(_faces)),
    _areaMagField(geomFields.areaMag),
    _faceAreaMag(dynamic_cast<const TArray&>(_areaMagField[_faces])),
    _areaField(geomFields.area),
    _faceArea(dynamic_cast<const VectorT3Array&>(_areaField[_faces]))
    
  {}
    KineticModelOptions<X>&   getOptions() {return _options;}  
  void applyRelaxWallBC(int f,const VectorX3&  WallVelocity,const X& WallTemperature) const
  {
    
    const double pi=_options.pi;
    const double epsilon=_options.epsilon_ES;
    
    const int c0 = _faceCells(f,0);
    const int c1 = _faceCells(f,1); ///changed
    
    if (_ibType[c0] != Mesh::IBTYPE_FLUID)
      return;
    
   
    const int numDirections = _quadrature.getDirCount();
    const XArray& cx = dynamic_cast<const XArray&>(*_quadrature.cxPtr);
    const XArray& cy = dynamic_cast<const XArray&>(*_quadrature.cyPtr);
    const XArray& cz = dynamic_cast<const XArray&>(*_quadrature.czPtr);
    const XArray& dcxyz= dynamic_cast<const XArray&>(*_quadrature.dcxyzPtr);
    VectorX3Array& v = dynamic_cast<VectorX3Array&>(_macroFields.velocity[_cells]);
    XArray& density  = dynamic_cast<XArray&>(_macroFields.density[_cells]); 
    XArray& collfreq  = dynamic_cast<XArray&>(_macroFields.collisionFrequency[_cells]);
    //XArray& pressure  = dynamic_cast<XArray&>(_macroFields.pressure[_cells]); 
    XArray& temperature  = dynamic_cast<XArray&>(_macroFields.temperature[_cells]);   
    const X uwall = WallVelocity[0];
    const X vwall = WallVelocity[1];
    const X wwall = WallVelocity[2];
    //cout << "uwall " << uwall << endl;
    const X Twall = WallTemperature;

    v[c1][0]=uwall;
    v[c1][1]=vwall;
    v[c1][2]=wwall;
  
    temperature[c1]=Twall;
    X Nmr(0.0) ;
    X Dmr(0.0) ;
    X incomFlux(0.0);
    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);
	const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
	const X c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];
	const X wallV_dot_en = uwall*en[0]+vwall*en[1]+wwall*en[2];
        const X fwall = 1.0/pow(pi*Twall,1.5)*exp(-(pow(cx[j]-uwall,2.0)+pow(cy[j]-vwall,2.0)+pow(cz[j]-wwall,2.0))/Twall);
        const X fndEq = *_dsfEqPtr.dsf[j];
        

  //      Field& fndEq = *_dsfEqPtr.dsf[j];
  //      const TArray& fgam = dynamic_cast<const TArray&>(fndEq[cells]);


	if (c_dot_en -wallV_dot_en < T_Scalar(epsilon)) //incoming
	  {
	    Dmr = Dmr - fwall*dcxyz[j]*(c_dot_en-wallV_dot_en);
	    incomFlux=incomFlux-dsf[c0]*dcxyz[j]*(c_dot_en -wallV_dot_en);
	  }
	else
	  {
            timetowall = 0
	    Nmr = Nmr + (fndEq-(fndEq-dsf[c0]))/exp(timetowall*collfreq)*dcxyz[j]*(c_dot_en -wallV_dot_en);
	  }	
      }
    const X nwall = Nmr/Dmr; // wall number density for initializing Maxwellian
    density[c1]=nwall;
    //if (c0==80)cout <<"incoming" << incomFlux <<" outgoing" <<Nmr <<endl;
    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);
	const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
	const X c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];	
	const X wallV_dot_en = uwall*en[0]+vwall*en[1]+wwall*en[2];

	if (c_dot_en-wallV_dot_en < T_Scalar(epsilon))
	  {
	    dsf[c1] = nwall/pow(pi*Twall,1.5)*exp(-(pow(cx[j]-uwall,2.0)+pow(cy[j]-vwall,2.0)+pow(cz[j]-wwall,2.0))/Twall);
	    //dsf[c0]=dsf[c1];  // change value of fluid boundary-cell
	  }
	else
	  dsf[c1]=dsf[c0];  
      }  
    
   
   
  }

void applyRelaxWallBC(const VectorX3& bVelocity,const X& bTemperature) const
  {
    for (int i=0; i<_faces.getCount();i++)
    applyRelaxWallBC(i,bVelocity,bTemperature);
  }
  
  
 void applyRelaxWallBC(const FloatValEvaluator<VectorX3>& bVelocity,const FloatValEvaluator<X>& bTemperature)const
  {
    for (int i=0; i<_faces.getCount();i++)
      applyRelaxWallBC(i,bVelocity[i],bTemperature[i]);

  }

  // Real wall with momentum accomodation coefficient 
 void applyRealWallBC(int f,const VectorX3&  WallVelocity,const X& WallTemperature,const X& accommodationCoefficient,const vector<int>& vecReflection) const
  {
    
    const double pi=_options.pi;
    //const double epsilon=_options.epsilon_ES;
    
    const int c0 = _faceCells(f,0);
    const int c1 = _faceCells(f,1); 
    
    if (_ibType[c0] != Mesh::IBTYPE_FLUID)
      return;
    
   
    const int numDirections = _quadrature.getDirCount();
    const XArray& cx = dynamic_cast<const XArray&>(*_quadrature.cxPtr);
    const XArray& cy = dynamic_cast<const XArray&>(*_quadrature.cyPtr);
    const XArray& cz = dynamic_cast<const XArray&>(*_quadrature.czPtr);
    const XArray& dcxyz= dynamic_cast<const XArray&>(*_quadrature.dcxyzPtr);
    VectorX3Array& v = dynamic_cast<VectorX3Array&>(_macroFields.velocity[_cells]);
    XArray& density  = dynamic_cast<XArray&>(_macroFields.density[_cells]); 
    XArray& temperature  = dynamic_cast<XArray&>(_macroFields.temperature[_cells]);   
    const X uwall = WallVelocity[0];
    const X vwall = WallVelocity[1];
    const X wwall = WallVelocity[2];
    const X Twall = WallTemperature;
    const X alpha = accommodationCoefficient;
    X m1alpha=1.0-alpha;
    v[c1][0]=uwall;
    v[c1][1]=vwall;
    v[c1][2]=wwall;
  
    temperature[c1]=Twall;
    X Nmr(0.0) ;
    X Dmr(0.0) ;
    X incomFlux(0.0);
    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);
	const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
	const X c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];
	const X wallV_dot_en = uwall*en[0]+vwall*en[1]+wwall*en[2];
	const X fwall = 1.0/pow(pi*Twall,1.5)*exp(-(pow(cx[j]-uwall,2.0)+pow(cy[j]-vwall,2.0)+pow(cz[j]-wwall,2.0))/Twall);
	if (c_dot_en -wallV_dot_en < T_Scalar(0.0)) //incoming
	  {
	    Dmr = Dmr - fwall*dcxyz[j]*(c_dot_en-wallV_dot_en);
	    incomFlux=incomFlux-dsf[c0]*dcxyz[j]*(c_dot_en-wallV_dot_en);
	  }
	else
	  {
	   Nmr = Nmr + dsf[c0]*dcxyz[j]*(c_dot_en-wallV_dot_en);
	  }	
      }
    const X nwall = Nmr/Dmr; // wall number density for initializing Maxwellian
    density[c1]=nwall;
    //if (c0==80)cout <<"incoming " << incomFlux <<" outgoing " <<Nmr << " dens " << nwall<< endl;
    //update f in boundary cell =alpha*f_wall+(1-alpha)*f_reflection
    //c.en-vwall.en=vwall.en-c_incident.en

    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);
	const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
	const X c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];	
	const X wallV_dot_en = uwall*en[0]+vwall*en[1]+wwall*en[2];

	// needed for method 1
	/*
	const int N2 = _quadrature.getNthetaCount();
	const int N3 = _quadrature.getNphiCount();
	const X dcx =  _quadrature.get_dcx();
	const X dcy = _quadrature.get_dcy();
	const X dcz = _quadrature.get_dcz();
	*/
	if (c_dot_en-wallV_dot_en < T_Scalar(0.0))
	  { 
	    /*
	    const X cx_incident = cx[j] - 2.0*(c_dot_en-wallV_dot_en)*en[0];
	    const X cy_incident = cy[j] - 2.0*(c_dot_en-wallV_dot_en)*en[1];
	    const X cz_incident = cz[j] - 2.0*(c_dot_en-wallV_dot_en)*en[2];    	   
	    
	    //method 1	    
	    const X i_incident = int((cx_incident-cx[0])/dcx+0.5);
	    const X j_incident = int((cy_incident-cy[0])/dcy+0.5);
	    const X k_incident = int((cz_incident-cz[0])/dcz+0.5);
	    const int direction_incident = k_incident+N3*j_incident+N3*N2*i_incident;
	    
	    //method 2
	    int direction_incident=0; 
	    X Rdotprod=1e54;
	    X dotprod=0.0;
	    for (int js=0; js<numDirections; js++){
	      dotprod=pow(cx_incident-cx[js],2)+pow(cy_incident-cy[js],2)+pow(cz_incident-cz[js],2);
		if (dotprod< Rdotprod){
		  Rdotprod =dotprod;
		  direction_incident=js;}
	    }
	   
	    */
	    const int direction_incident = vecReflection[j];
	    Field& fndi = *_dsfPtr.dsf[direction_incident];
	    const XArray& dsfi = dynamic_cast<const XArray&>(fndi[_cells]);
	    
	    dsf[c1] = alpha*nwall/pow(pi*Twall,1.5)*exp(-(pow(cx[j]-uwall,2.0)+pow(cy[j]-vwall,2.0)+pow(cz[j]-wwall,2.0))/Twall)+m1alpha*dsfi[c0]; //write into boundary;
	    //dsf[c0]=dsf[c1]; //write into boundary cell
	  }
	else
	  dsf[c1]=dsf[c0];  
      }  
    
   
   
  }

 void applyRealWallBC(const VectorX3& bVelocity,const X& bTemperature,const X& accomCoeff,const vector<int>& vecReflection) const
  {
    for (int i=0; i<_faces.getCount();i++)
      applyRealWallBC(i,bVelocity,bTemperature,accomCoeff,vecReflection);
  }
  
 
  void applyRealWallBC(const FloatValEvaluator<VectorX3>& bVelocity,const FloatValEvaluator<X>& bTemperature,const FloatValEvaluator<X>& accomCoeff,const vector<int>& vecReflection) const
  {
    for (int i=0; i<_faces.getCount();i++)
      applyRealWallBC(i,bVelocity[i],bTemperature[i],accomCoeff[i],vecReflection);

  }
 
  void applySpecularWallBC(int f,const vector<int>& vecReflection) const
  {
    
    const int c0 = _faceCells(f,0); //interior
    const int c1 = _faceCells(f,1); //boundary cell
    
    if (_ibType[c0] != Mesh::IBTYPE_FLUID)
      return;
    
    const int numDirections = _quadrature.getDirCount();
    const XArray& cx = dynamic_cast<const XArray&>(*_quadrature.cxPtr);
    const XArray& cy = dynamic_cast<const XArray&>(*_quadrature.cyPtr);
    const XArray& cz = dynamic_cast<const XArray&>(*_quadrature.czPtr);
    
    for (int j=0; j<numDirections; j++)
      {
	// Find incident molecule directions
	const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
	const X  c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];
	
	Field& fndw = *_dsfPtr.dsf[j];
	XArray& dsfw = dynamic_cast<XArray&>(fndw[_cells]);
	if(c_dot_en < T_Scalar(0.0)) //incoming molecules to interior
	  {
	    const int direction_incident = vecReflection[j];
	    Field& fnd = *_dsfPtr.dsf[direction_incident];
	    const XArray& dsf = dynamic_cast<const XArray&>(fnd[_cells]);
	    dsfw[c1] = dsf[c0]; //write into boundary
	  }
	else{
	  dsfw[c1]=dsfw[c0];}
      }
    
    
  }   
  
  void applySpecularWallBC(const vector<int>& vecReflection) const
  {    
    for (int i=0; i<_faces.getCount();i++){
      applySpecularWallBC(i,vecReflection);  
      // applySpecularWallBC_Cartesian(i);
    }
    
  }
  
  void applySpecularWallBC_Cartesian(int f) const
  {
    
    const int c0 = _faceCells(f,0); //interior
    const int c1 = _faceCells(f,1); //boundary cell

    if (_ibType[c0] != Mesh::IBTYPE_FLUID)
      return;
    
    const int numDirections = _quadrature.getDirCount();
    const XArray& cx = dynamic_cast<const XArray&>(*_quadrature.cxPtr);
    const XArray& cy = dynamic_cast<const XArray&>(*_quadrature.cyPtr);
    const XArray& cz = dynamic_cast<const XArray&>(*_quadrature.czPtr);
   
    const int N2 = _quadrature.getNthetaCount();
    const int N3 = _quadrature.getNphiCount();
    const X dcx =  _quadrature.get_dcx();
    const X dcy = _quadrature.get_dcy();
    const X dcz = _quadrature.get_dcz();
   
    for (int j=0; j<numDirections; j++)
      {
	// Find incident molecule directions
	const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
	const X  c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];
	
	Field& fndw = *_dsfPtr.dsf[j];
	XArray& dsfw = dynamic_cast<XArray&>(fndw[_cells]);
	if(c_dot_en < T_Scalar(0.0)) //incoming molecules to interior
	  {
	   
	    const X cx_incident = cx[j] - 2.0*c_dot_en*en[0];
	    const X cy_incident = cy[j] - 2.0*c_dot_en*en[1];
	    const X cz_incident = cz[j] - 2.0*c_dot_en*en[2];    	   
	    
	    const X i_incident = int((cx_incident-cx[0])/dcx+0.5);
	    const X j_incident = int((cy_incident-cy[0])/dcy+0.5);
	    const X k_incident = int((cz_incident-cz[0])/dcz+0.5);
	    const int direction_incident = k_incident+N3*j_incident+N3*N2*i_incident;
	   	    
	    Field& fnd = *_dsfPtr.dsf[direction_incident];
	    const XArray& dsf = dynamic_cast<const XArray&>(fnd[_cells]);
	    
	    dsfw[c1] = dsf[c0]; //write into boundary
	    
	  }
	else{
	  dsfw[c1]=dsfw[c0];}
      }
    
        
  }   

  void applyZeroGradientBC(int f) const
  {
    const int c0 = _faceCells(f,0);
    const int c1 = _faceCells(f,1);
    
    if (_ibType[c0] != Mesh::IBTYPE_FLUID)
      return;
    
    const int numDirections = _quadrature.getDirCount();
    
    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);
	dsf[c1]=dsf[c0];
      }
  }
  void applyZeroGradientBC() const
  {
    for (int i=0; i<_faces.getCount();i++)
      applyZeroGradientBC(i);
  } 
  
  void applyPressureInletBC(int f,const X& inletTemperature,const X& inletPressure)  const
  {
    
   const double pi=_options.pi;  
    const int c0 = _faceCells(f,0);
    const int c1 = _faceCells(f,1);
    
    if (_ibType[c0] != Mesh::IBTYPE_FLUID)
      return;
    
   
    const int numDirections = _quadrature.getDirCount();
    const XArray& cx = dynamic_cast<const XArray&>(*_quadrature.cxPtr);
    const XArray& cy = dynamic_cast<const XArray&>(*_quadrature.cyPtr);
    const XArray& cz = dynamic_cast<const XArray&>(*_quadrature.czPtr); 
    const XArray& wts = dynamic_cast<const XArray&>(*_quadrature.dcxyzPtr);
    XArray& density  = dynamic_cast<XArray&>(_macroFields.density[_cells]); 
    XArray& pressure  = dynamic_cast<XArray&>(_macroFields.pressure[_cells]); 
    XArray& temperature  = dynamic_cast<XArray&>(_macroFields.temperature[_cells]);   
    VectorX3Array& v = dynamic_cast<VectorX3Array&>(_macroFields.velocity[_cells]);
    
   
     X uwallnew = 0.0; 
     
    const X Tin = inletTemperature;
    const X Pin = inletPressure;
    const X nin = Pin/Tin; // wall number density for initializing Maxwellian

    //store boundary values
    temperature[c1]=inletTemperature;
    pressure[c1]=inletPressure;
    density[c1]=nin;
    v[c1][0]=0.0;
    v[c1][1]=0.0;
    v[c1][2]=0.0;

    const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
    
   
    //update velocity
    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);
	dsf[c1] = nin/pow(pi*Tin,1.5)*exp(-(pow(cx[j]-v[c1][0],2.0)+pow(cy[j]-v[c1][1],2.0)+pow(cz[j]-v[c1][2],2.0))/Tin);
	
	   const X c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];

	   if (abs(en[0]) == T_Scalar(1.0)) 
	     {
	       if( c_dot_en > T_Scalar(0.0))
		 {
		   uwallnew=uwallnew+cx[j]*dsf[c0]*wts[j];
		 }
	       else {uwallnew=uwallnew+cx[j]*dsf[c1]*wts[j];}
	     } 
	   else if (abs(en[1]) == T_Scalar(1.0)) 
	     {
	       if( c_dot_en > T_Scalar(0.0))
		 {
		   uwallnew=uwallnew+cy[j]*dsf[c0]*wts[j];
		 }
	       else {uwallnew=uwallnew+cy[j]*dsf[c1]*wts[j];}
	     }  
	   else if (abs(en[2]) == T_Scalar(1.0)) 
	     {
	       if( c_dot_en > T_Scalar(0.0))
		 {
		   uwallnew=uwallnew+cz[j]*dsf[c0]*wts[j];
		 }
	       else {uwallnew=uwallnew+cz[j]*dsf[c1]*wts[j];}
	     }
      }
    /*
    if (abs(en[0]) == T_Scalar(1.0))     
      v[c1][0]=uwallnew/nin;
    else if (abs(en[1]) == T_Scalar(1.0)) //incoming  {vwallnew=vwallnew+cy[j]*dsf[c1]*wts[j];}
      v[c1][1]=uwallnew/nin;
    else if(abs(en[2]) == T_Scalar(1.0))     
      v[c1][2]=uwallnew/nin;//uwall=uwall+relax*(uwallnew-uwall);
    */
    

    //update f
    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);
	const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
	const X c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];		
	if (c_dot_en< T_Scalar(0.0))
	  {
	    dsf[c1] = nin/pow(pi*Tin,1.5)*exp(-(pow(cx[j]-v[c1][0],2.0)+pow(cy[j]-v[c1][1],2.0)+pow(cz[j]-v[c1][2],2.0))/Tin);
	    // dsf[c0]=dsf[c1];// change value of fluid boundary-cell
	  }
	else{dsf[c1]=dsf[c0];}//outgoing
	
      }
    
}
  

  
  
  void applyPressureInletBC(const FloatValEvaluator<X>& bTemperature,const FloatValEvaluator<X>& bPresssure) const
  {
    for (int i=0; i<_faces.getCount();i++)
      applyPressureInletBC(i,bTemperature[i],bPresssure[i]);

  } 
  

  // velocity inlet

 void applyVelocityInletBC(int f,const X& inletTemperature,const VectorX3& inletVelocity)  const
  {
   
     const double pi=_options.pi;
    const int c0 = _faceCells(f,0);
    const int c1 = _faceCells(f,1);
    
    if (_ibType[c0] != Mesh::IBTYPE_FLUID)
      return;
    
   
    const int numDirections = _quadrature.getDirCount();
    const XArray& cx = dynamic_cast<const XArray&>(*_quadrature.cxPtr);
    const XArray& cy = dynamic_cast<const XArray&>(*_quadrature.cyPtr);
    const XArray& cz = dynamic_cast<const XArray&>(*_quadrature.czPtr); 
    XArray& density  = dynamic_cast<XArray&>(_macroFields.density[_cells]); 
    XArray& temperature  = dynamic_cast<XArray&>(_macroFields.temperature[_cells]);   
    VectorX3Array& v = dynamic_cast<VectorX3Array&>(_macroFields.velocity[_cells]);
    
   
    const X Tin = inletTemperature;
    X nin = density[c0];
  
    //store boundary values
    temperature[c1]=inletTemperature;
    density[c1]=nin;
   
    v[c1][0]=inletVelocity[0];
    v[c1][1]=0.0;
    v[c1][2]=0.0;

    //update f
    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);
	const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
	const X c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];		
	if (c_dot_en< T_Scalar(0.0))
	  {
	    dsf[c1] = nin/pow(pi*Tin,1.5)*exp(-(pow(cx[j]-v[c1][0],2.0)+pow(cy[j]-v[c1][1],2.0)+pow(cz[j]-v[c1][2],2.0))/Tin);
	    //dsf[c0] = dsf[c1];// change value of fluid boundary-cell
	  }
	else{dsf[c1]=dsf[c0];}//outgoing
	
      }
  
}
  

  
  
  void applyVelocityInletBC(const FloatValEvaluator<X>& bTemperature,const FloatValEvaluator<VectorX3>& bVelocity) const
  {
    for (int i=0; i<_faces.getCount();i++)
      applyVelocityInletBC(i,bTemperature[i],bVelocity[i]);

  } 


  //outlet Boundary Condition
  void applyPressureOutletBC(int f,const X& outletTemperature,const X& outletPressure) const
  { 
   
    const double pi=_options.pi;
    const int c0 = _faceCells(f,0);
    const int c1 = _faceCells(f,1);
    
    if (_ibType[c0] != Mesh::IBTYPE_FLUID)
      return;
    
    const int numDirections = _quadrature.getDirCount();
    const XArray& cx = dynamic_cast<const XArray&>(*_quadrature.cxPtr);
    const XArray& cy = dynamic_cast<const XArray&>(*_quadrature.cyPtr);
    const XArray& cz = dynamic_cast<const XArray&>(*_quadrature.czPtr);
    XArray& density  = dynamic_cast<XArray&>(_macroFields.density[_cells]);
    VectorX3Array& v = dynamic_cast<VectorX3Array&>(_macroFields.velocity[_cells]);
    XArray& pressure  = dynamic_cast<XArray&>(_macroFields.pressure[_cells]);  
    XArray& temperature  = dynamic_cast<XArray&>(_macroFields.temperature[_cells]);  
   
    // X Tout = outletTemperature;
    const X Pout = outletPressure;
    X Pcell= pressure[c0];  
    X gamma =_options.SpHeatRatio;
    const VectorT3 en = _faceArea[f]/_faceAreaMag[f];
    X nout =density[c0];
    
    if (Pcell > Pout){
      const X avel2 =gamma*Pcell/density[c0]; //velcotiy of sound square
      nout =density[c0]-(Pcell-Pout)/avel2;
      X ubulk=pow(pow(v[c0][0],2.0)+pow(v[c0][1],2.0)+pow(v[c0][2],2.0),0.5);
      X ucoeff=1.0/ubulk*(ubulk+(Pcell-Pout)/(pow(2.0*avel2,0.5)*density[c0]));
      if(abs(en[0])==T_Scalar(1.0))
	v[c1][0] = v[c0][0]*ucoeff; 
      //v[c1][0]=v[c0][0]+(pressure[c0]-Pout)/(pow(2.0*avel2,0.5)*density[c0]); //exit velocity
      else if(abs(en[1])==T_Scalar(1.0))
	v[c1][1] = v[c0][1]*ucoeff;
      else if(abs(en[2])==T_Scalar(1.0))
	v[c1][2] = v[c0][2]*ucoeff;
      
      density[c1]=nout; pressure[c1]=Pout; //update macroPr
      temperature[c1]=Pout/nout;
    }
    else{
      
      density[c1]=density[c0];
      pressure[c1]=pressure[c0];
      temperature[c1]=temperature[c0];
      
    }
    for (int j=0; j<numDirections; j++)
      {
	Field& fnd = *_dsfPtr.dsf[j];
	XArray& dsf = dynamic_cast< XArray&>(fnd[_cells]);

	const X c_dot_en = cx[j]*en[0]+cy[j]*en[1]+cz[j]*en[2];	
	if (c_dot_en < T_Scalar(0.0))
	  {
	    dsf[c1] = nout/pow(pi*temperature[c1],1.5)*exp(-(pow(cx[j]-v[c1][0],2.0)+pow(cy[j]-v[c1][1],2.0)+pow(cz[j]-v[c1][2],2.0))/temperature[c1]);
	    // dsf[c0]=dsf[c1];// change value of fluid boundary-cell
	  }
	else{dsf[c1]=dsf[c0];}
	
      }
    
    
}

  void applyPressureOutletBC(const X& bTemperature, const X& bPressure) const
  {
    for (int i=0; i<_faces.getCount();i++)
      applyPressureOutletWallBC(i,bTemperature,bPressure);
  }
  
  
  void applyPressureOutletBC(const FloatValEvaluator<X>& bTemperature,const FloatValEvaluator<X>& bPresssure) const
  {
    for (int i=0; i<_faces.getCount();i++)
      applyPressureOutletBC(i,bTemperature[i],bPresssure[i]);

  }

  /*
  void applyNSInterfaceBC(const FloatValEvaluator<X>& bTemperature,const FloatValEvaluator<X>& bDensity,const FloatValEvaluator<VectorX3>& bVelocity) const
   {
fin(j3,j2,j1,i)=(rhoNS_L/(pi*TNS_L)**1.5*&
                       exp(-((cx(j1)-uNS_L)**2.0+(cy(j2)-vNS_L)**2.0+(cz(j3))**2.0)/TNS_L)) *(1-2*cx(j1)*cy(j2)*slopeL*(muref/mu0*(Temp(i)/T_ref_mu*T_init)**w))
fin(j3,j2,j1,i)=(rhoNS_R/(pi*TNS_R)**1.5*
                      exp(-((cx(j1)-uNS_R)**2.0+(cy(j2)-vNS_R)**2.0+(cz(j3))**2.0)/TNS_R)) 
                      *(1-1.5*cx(j1)*((cx(j1)-uNS_R)**2.0+(cy(j2)-vNS_R)**2.0+cz(j3)**2.0-2.5)
                       *(muref/mu0*(TNS_R/T_ref_mu*T_init)**w)*slopeR)
 }
  */
 protected:

  const StorageSite& _faces;
  const StorageSite& _cells;
  const Array<int>& _ibType;
  const Quadrature<X>& _quadrature;
  MacroFields& _macroFields;
  const DistFunctFields<X>& _dsfPtr;
  const CRConnectivity& _faceCells;
  const Field& _areaMagField;
  const TArray& _faceAreaMag;
  const Field& _areaField;
  const VectorT3Array& _faceArea;
  KineticModelOptions<X> _options;
};
  
#endif
