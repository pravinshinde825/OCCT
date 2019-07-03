// Created on: 2017-07-25
// Created by: Anastasia BOBYLEVA
// Copyright (c) 2017-2019 OPEN CASCADE SAS
//
// This file is part of Open CASCADE Technology software library.
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 2.1 as published
// by the Free Software Foundation, with special exception defined in the file
// OCCT_LGPL_EXCEPTION.txt. Consult the file LICENSE_LGPL_21.txt included in OCCT
// distribution for complete text of the license and disclaimer of any warranty.
//
// Alternatively, this file may be used under the terms of Open CASCADE
// commercial license or contractual agreement.

#include <AIS_ViewCube.hxx>

#include <AIS_AnimationCamera.hxx>
#include <AIS_InteractiveContext.hxx>
#include <gp_Ax2.hxx>
#include <Graphic3d_ViewAffinity.hxx>
#include <NCollection_Lerp.hxx>
#include <Prs3d.hxx>
#include <Prs3d_Arrow.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Prs3d_Root.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_ToolDisk.hxx>
#include <Prs3d_ToolSphere.hxx>
#include <Select3D_SensitivePrimitiveArray.hxx>
#include <SelectMgr_SequenceOfOwner.hxx>
#include <V3d.hxx>
#include <V3d_View.hxx>

IMPLEMENT_STANDARD_RTTIEXT(AIS_ViewCube, AIS_InteractiveObject)
IMPLEMENT_STANDARD_RTTIEXT(AIS_ViewCubeOwner, SelectMgr_EntityOwner)

namespace
{
  static const Standard_Integer THE_NB_ROUND_SPLITS = 8;
  static const Standard_Integer THE_NB_DISK_SLICES = 20;
  static const Standard_Integer THE_NB_ARROW_FACETTES = 20;

  //! Return the number of non-zero components.
  static Standard_Integer nbDirectionComponents (const gp_Dir& theDir)
  {
    Standard_Integer aNbComps = 0;
    for (Standard_Integer aCompIter = 1; aCompIter <= 3; ++aCompIter)
    {
      if (Abs (theDir.Coord (aCompIter)) > gp::Resolution())
      {
        ++aNbComps;
      }
    }
    return aNbComps;
  }
}

//! Simple sensitive element for picking by point only.
class AIS_ViewCubeSensitive : public Select3D_SensitivePrimitiveArray
{
  DEFINE_STANDARD_RTTI_INLINE(AIS_ViewCubeSensitive, Select3D_SensitivePrimitiveArray)
public:
  //! Constructor.
  AIS_ViewCubeSensitive (const Handle(SelectMgr_EntityOwner)& theOwner,
                         const Handle(Graphic3d_ArrayOfTriangles)& theTris)
  : Select3D_SensitivePrimitiveArray (theOwner)
  {
    InitTriangulation (theTris->Attributes(), theTris->Indices(), TopLoc_Location());
  }

  //! Checks whether element overlaps current selecting volume.
  virtual Standard_Boolean Matches (SelectBasics_SelectingVolumeManager& theMgr,
                                    SelectBasics_PickResult& thePickResult) Standard_OVERRIDE
  {
    return isValidRay (theMgr)
        && Select3D_SensitivePrimitiveArray::Matches (theMgr, thePickResult);
  }

  //! Checks if picking ray can be used for detection.
  bool isValidRay (const SelectBasics_SelectingVolumeManager& theMgr) const
  {
    if (theMgr.GetActiveSelectionType() != SelectBasics_SelectingVolumeManager::Point)
    {
      // disallow rectangular selection
      return false;
    }

    if (AIS_ViewCubeOwner* anOwner = dynamic_cast<AIS_ViewCubeOwner* >(myOwnerId.get()))
    {
      const Standard_Real anAngleToler = 10.0 * M_PI / 180.0;
      const gp_Vec aRay (theMgr.GetNearPickedPnt(), theMgr.GetFarPickedPnt());
      const gp_Dir aDir = V3d::GetProjAxis (anOwner->MainOrientation());
      return !aRay.IsNormal (aDir, anAngleToler);
    }
    return true;
  }
};

//=======================================================================
//function : IsBoxSide
//purpose  :
//=======================================================================
bool AIS_ViewCube::IsBoxSide (V3d_TypeOfOrientation theOrient)
{
  return nbDirectionComponents (V3d::GetProjAxis (theOrient)) == 1;
}

//=======================================================================
//function : IsBoxEdge
//purpose  :
//=======================================================================
bool AIS_ViewCube::IsBoxEdge (V3d_TypeOfOrientation theOrient)
{
  return nbDirectionComponents (V3d::GetProjAxis (theOrient)) == 2;
}

//=======================================================================
//function : IsBoxCorner
//purpose  :
//=======================================================================
bool AIS_ViewCube::IsBoxCorner (V3d_TypeOfOrientation theOrient)
{
  return nbDirectionComponents (V3d::GetProjAxis (theOrient)) == 3;
}

//=======================================================================
//function : AIS_ViewCube
//purpose  :
//=======================================================================
AIS_ViewCube::AIS_ViewCube()
: myBoxEdgeAspect (new Prs3d_ShadingAspect()),
  myBoxCornerAspect (new Prs3d_ShadingAspect()),
  mySize (1.0),
  myBoxEdgeMinSize (2.0),
  myBoxEdgeGap (0.0),
  myBoxFacetExtension (1.0),
  myAxesPadding (1.0),
  myCornerMinSize (2.0),
  myRoundRadius  (0.0),
  myToDisplayAxes (true),
  myToDisplayEdges (true),
  myToDisplayVertices (true),
  myIsYup (false),
  myViewAnimation (new AIS_AnimationCamera ("AIS_ViewCube", Handle(V3d_View)())),
  myStartState(new Graphic3d_Camera()),
  myEndState  (new Graphic3d_Camera()),
  myDuration (0.5),
  myToAutoStartAnim (true),
  myIsFixedAnimation (true),
  myToFitSelected (true),
  myToResetCameraUp (false)
{
  myInfiniteState = true;
  myIsMutable = true;
  myDrawer->SetZLayer (Graphic3d_ZLayerId_Topmost);
  myTransformPersistence = new Graphic3d_TransformPers (Graphic3d_TMF_TriedronPers, Aspect_TOTP_LEFT_LOWER, Graphic3d_Vec2i (100, 100));

  myDrawer->SetTextAspect  (new Prs3d_TextAspect());
  myDrawer->SetShadingAspect (new Prs3d_ShadingAspect());

  myDynHilightDrawer = new Prs3d_Drawer();
  myDynHilightDrawer->SetLink (myDrawer);
  myDynHilightDrawer->SetShadingAspect (new Prs3d_ShadingAspect());

  setDefaultAttributes();
  setDefaultHighlightAttributes();

  // setup default labels
  myBoxSideLabels.Bind (V3d_TypeOfOrientation_Zup_Front,  "FRONT");
  myBoxSideLabels.Bind (V3d_TypeOfOrientation_Zup_Back,   "BACK");
  myBoxSideLabels.Bind (V3d_TypeOfOrientation_Zup_Top,    "TOP");
  myBoxSideLabels.Bind (V3d_TypeOfOrientation_Zup_Bottom, "BOTTOM");
  myBoxSideLabels.Bind (V3d_TypeOfOrientation_Zup_Left,   "LEFT");
  myBoxSideLabels.Bind (V3d_TypeOfOrientation_Zup_Right,  "RIGHT");

  myAxesLabels.Bind (Prs3d_DP_XAxis, "X");
  myAxesLabels.Bind (Prs3d_DP_YAxis, "Y");
  myAxesLabels.Bind (Prs3d_DP_ZAxis, "Z");

  // define default size
  SetSize (70.0);
}

//=======================================================================
//function : setDefaultAttributes
//purpose  :
//=======================================================================
void AIS_ViewCube::setDefaultAttributes()
{
  myDrawer->TextAspect()->SetHorizontalJustification(Graphic3d_HTA_CENTER);
  myDrawer->TextAspect()->SetVerticalJustification  (Graphic3d_VTA_CENTER);
  myDrawer->TextAspect()->SetColor (Quantity_NOC_BLACK);
  myDrawer->TextAspect()->SetFont (Font_NOF_SANS_SERIF);
  myDrawer->TextAspect()->SetHeight (16.0);
  // this should be forced back-face culling regardless Closed flag
  myDrawer->TextAspect()->Aspect()->SetSuppressBackFaces (true);

  Graphic3d_MaterialAspect aMat (Graphic3d_NOM_UserDefined);
  aMat.SetColor (Quantity_NOC_WHITE);
  aMat.SetAmbientColor (Quantity_NOC_GRAY60);

  const Handle(Graphic3d_AspectFillArea3d)& aShading = myDrawer->ShadingAspect()->Aspect();
  aShading->SetInteriorStyle (Aspect_IS_SOLID);
  // this should be forced back-face culling regardless Closed flag
  aShading->SetSuppressBackFaces (true);
  aShading->SetInteriorColor (aMat.Color());
  aShading->SetFrontMaterial (aMat);
  myDrawer->SetFaceBoundaryDraw (false);

  *myBoxEdgeAspect  ->Aspect() = *aShading;
  myBoxEdgeAspect->SetColor (Quantity_NOC_GRAY30);
  *myBoxCornerAspect->Aspect() = *aShading;
  myBoxCornerAspect->SetColor (Quantity_NOC_GRAY30);
}

//=======================================================================
//function : setDefaultHighlightAttributes
//purpose  :
//=======================================================================
void AIS_ViewCube::setDefaultHighlightAttributes()
{
  Graphic3d_MaterialAspect aHighlightMaterial;
  aHighlightMaterial.SetReflectionModeOff (Graphic3d_TOR_AMBIENT);
  aHighlightMaterial.SetReflectionModeOff (Graphic3d_TOR_DIFFUSE);
  aHighlightMaterial.SetReflectionModeOff (Graphic3d_TOR_SPECULAR);
  aHighlightMaterial.SetReflectionModeOff (Graphic3d_TOR_EMISSION);
  aHighlightMaterial.SetMaterialType (Graphic3d_MATERIAL_ASPECT);
  myDynHilightDrawer->SetShadingAspect (new Prs3d_ShadingAspect());
  myDynHilightDrawer->ShadingAspect()->SetMaterial (aHighlightMaterial);
  myDynHilightDrawer->ShadingAspect()->SetColor (Quantity_NOC_CYAN1);
  myDynHilightDrawer->SetZLayer (Graphic3d_ZLayerId_Topmost);
  myDynHilightDrawer->SetColor (Quantity_NOC_CYAN1);
}

//=======================================================================
//function : SetYup
//purpose  :
//=======================================================================
void AIS_ViewCube::SetYup (Standard_Boolean theIsYup,
                           Standard_Boolean theToUpdateLabels)
{
  if (myIsYup == theIsYup)
  {
    return;
  }

  myIsYup = theIsYup;

  static const V3d_TypeOfOrientation THE_ZUP_ORI_LIST[6] =
  {
    V3d_TypeOfOrientation_Zup_Front, V3d_TypeOfOrientation_Zup_Back,
    V3d_TypeOfOrientation_Zup_Top,   V3d_TypeOfOrientation_Zup_Bottom,
    V3d_TypeOfOrientation_Zup_Left,  V3d_TypeOfOrientation_Zup_Right
  };
  static const V3d_TypeOfOrientation THE_YUP_ORI_LIST[6] =
  {
    V3d_TypeOfOrientation_Yup_Front, V3d_TypeOfOrientation_Yup_Back,
    V3d_TypeOfOrientation_Yup_Top,   V3d_TypeOfOrientation_Yup_Bottom,
    V3d_TypeOfOrientation_Yup_Left,  V3d_TypeOfOrientation_Yup_Right
  };
  if (theToUpdateLabels)
  {
    NCollection_Array1<TCollection_AsciiString> aLabels (0, 5);
    for (Standard_Integer aLabelIter = 0; aLabelIter < 6; ++aLabelIter)
    {
      myBoxSideLabels.Find (!myIsYup ? THE_YUP_ORI_LIST[aLabelIter] : THE_ZUP_ORI_LIST[aLabelIter],
                            aLabels.ChangeValue (aLabelIter));
    }
    for (Standard_Integer aLabelIter = 0; aLabelIter < 6; ++aLabelIter)
    {
      myBoxSideLabels.Bind (myIsYup ? THE_YUP_ORI_LIST[aLabelIter] : THE_ZUP_ORI_LIST[aLabelIter],
                            aLabels.Value (aLabelIter));
    }
  }

  SetToUpdate();
}

//=======================================================================
//function : ResetStyles
//purpose  :
//=======================================================================
void AIS_ViewCube::ResetStyles()
{
  UnsetAttributes();
  UnsetHilightAttributes();

  myBoxEdgeMinSize = 2.0;
  myCornerMinSize  = 2.0;
  myBoxEdgeGap     = 0.0;
  myRoundRadius    = 0.0;

  myToDisplayAxes = true;
  myToDisplayEdges = true;
  myToDisplayVertices = true;

  myBoxFacetExtension = 1.0;
  myAxesPadding = 1.0;
  SetSize (70.0);
}

//=======================================================================
//function : SetSize
//purpose  :
//=======================================================================
void AIS_ViewCube::SetSize (Standard_Real theValue,
                            Standard_Boolean theToAdaptAnother)
{
  const bool isNewSize = Abs (mySize - theValue) > Precision::Confusion();
  mySize = theValue;
  if (theToAdaptAnother)
  {
    if (myBoxFacetExtension > 0.0)
    {
      SetBoxFacetExtension (mySize * 0.15);
    }
    if (myAxesPadding > 0.0)
    {
      SetAxesPadding (mySize * 0.1);
    }
    SetFontHeight (mySize * 0.16);
  }
  if (isNewSize)
  {
    SetToUpdate();
  }
}

//=======================================================================
//function : SetRoundRadius
//purpose  :
//=======================================================================
void AIS_ViewCube::SetRoundRadius (const Standard_Real theValue)
{
  Standard_OutOfRange_Raise_if (theValue < 0.0 || theValue > 0.5,
                                "AIS_ViewCube::SetRoundRadius(): theValue should be in [0; 0.5]");
  if (Abs (myRoundRadius - theValue) > Precision::Confusion())
  {
    myRoundRadius = theValue;
    SetToUpdate();
  }
}

//=======================================================================
//function : createRoundRectangleTriangles
//purpose  :
//=======================================================================
Handle(Graphic3d_ArrayOfTriangles) AIS_ViewCube::createRoundRectangleTriangles (const gp_XY& theSize,
                                                                                Standard_Real theRadius,
                                                                                const gp_Trsf& theTrsf)
{
  const Standard_Real aRadius = Min (theRadius, Min (theSize.X(), theSize.Y()) * 0.5);
  const gp_XY  aHSize (theSize.X() * 0.5 - aRadius, theSize.Y() * 0.5 - aRadius);
  const gp_Dir aNorm = gp::DZ().Transformed (theTrsf);
  Handle(Graphic3d_ArrayOfTriangles) aTris;
  if (aRadius > 0.0)
  {
    const Standard_Integer aNbNodes = (THE_NB_ROUND_SPLITS + 1) * 4 + 1;
    aTris = new Graphic3d_ArrayOfTriangles (aNbNodes, aNbNodes * 3, Graphic3d_ArrayFlags_VertexNormal);

    aTris->AddVertex (gp_Pnt (0.0, 0.0, 0.0).Transformed (theTrsf));
    for (Standard_Integer aNodeIter = 0; aNodeIter <= THE_NB_ROUND_SPLITS; ++aNodeIter)
    {
      const Standard_Real anAngle = NCollection_Lerp<Standard_Real>::Interpolate (M_PI * 0.5, 0.0, Standard_Real(aNodeIter) / Standard_Real(THE_NB_ROUND_SPLITS));
      aTris->AddVertex (gp_Pnt (aHSize.X() + aRadius * Cos (anAngle), aHSize.Y() + aRadius * Sin (anAngle), 0.0).Transformed (theTrsf));
    }
    for (Standard_Integer aNodeIter = 0; aNodeIter <= THE_NB_ROUND_SPLITS; ++aNodeIter)
    {
      const Standard_Real anAngle = NCollection_Lerp<Standard_Real>::Interpolate (0.0, -M_PI * 0.5, Standard_Real(aNodeIter) / Standard_Real(THE_NB_ROUND_SPLITS));
      aTris->AddVertex (gp_Pnt (aHSize.X() + aRadius * Cos (anAngle), -aHSize.Y() + aRadius * Sin (anAngle), 0.0).Transformed (theTrsf));
    }
    for (Standard_Integer aNodeIter = 0; aNodeIter <= THE_NB_ROUND_SPLITS; ++aNodeIter)
    {
      const Standard_Real anAngle = NCollection_Lerp<Standard_Real>::Interpolate (-M_PI * 0.5, -M_PI, Standard_Real(aNodeIter) / Standard_Real(THE_NB_ROUND_SPLITS));
      aTris->AddVertex (gp_Pnt (-aHSize.X() + aRadius * Cos (anAngle), -aHSize.Y() + aRadius * Sin (anAngle), 0.0).Transformed (theTrsf));
    }
    for (Standard_Integer aNodeIter = 0; aNodeIter <= THE_NB_ROUND_SPLITS; ++aNodeIter)
    {
      const Standard_Real anAngle = NCollection_Lerp<Standard_Real>::Interpolate (-M_PI, -M_PI * 1.5, Standard_Real(aNodeIter) / Standard_Real(THE_NB_ROUND_SPLITS));
      aTris->AddVertex (gp_Pnt (-aHSize.X() + aRadius * Cos (anAngle), aHSize.Y() + aRadius * Sin (anAngle), 0.0).Transformed (theTrsf));
    }

    // split triangle fan
    for (Standard_Integer aNodeIter = 2; aNodeIter <= aTris->VertexNumber(); ++aNodeIter)
    {
      aTris->AddEdge (1);
      aTris->AddEdge (aNodeIter - 1);
      aTris->AddEdge (aNodeIter);
    }
    aTris->AddEdge (1);
    aTris->AddEdge (aTris->VertexNumber());
    aTris->AddEdge (2);
  }
  else
  {
    aTris = new Graphic3d_ArrayOfTriangles (4, 6, Graphic3d_ArrayFlags_VertexNormal);
    aTris->AddVertex (gp_Pnt (-aHSize.X(), -aHSize.Y(), 0.0).Transformed (theTrsf));
    aTris->AddVertex (gp_Pnt (-aHSize.X(),  aHSize.Y(), 0.0).Transformed (theTrsf));
    aTris->AddVertex (gp_Pnt ( aHSize.X(),  aHSize.Y(), 0.0).Transformed (theTrsf));
    aTris->AddVertex (gp_Pnt ( aHSize.X(), -aHSize.Y(), 0.0).Transformed (theTrsf));
    aTris->AddEdges (3, 1, 2);
    aTris->AddEdges (1, 3, 4);
  }

  for (Standard_Integer aVertIter = 1; aVertIter <= aTris->VertexNumber(); ++aVertIter)
  {
    aTris->SetVertexNormal (aVertIter, -aNorm);
  }
  return aTris;
}

//=======================================================================
//function : createBoxPartTriangles
//purpose  :
//=======================================================================
Handle(Graphic3d_ArrayOfTriangles) AIS_ViewCube::createBoxPartTriangles (V3d_TypeOfOrientation theDir) const
{
  if (IsBoxSide (theDir))
  {
    return createBoxSideTriangles (theDir);
  }
  else if (IsBoxEdge (theDir)
        && myToDisplayEdges)
  {
    return createBoxEdgeTriangles (theDir);
  }
  else if (IsBoxCorner (theDir)
        && myToDisplayVertices)
  {
    return createBoxCornerTriangles (theDir);
  }
  return Handle(Graphic3d_ArrayOfTriangles)();
}

//=======================================================================
//function : createBoxSideTriangles
//purpose  :
//=======================================================================
Handle(Graphic3d_ArrayOfTriangles) AIS_ViewCube::createBoxSideTriangles (V3d_TypeOfOrientation theDirection) const
{
  const gp_Dir aDir = V3d::GetProjAxis (theDirection);
  const gp_Pnt aPos = aDir.XYZ() * (mySize * 0.5 + myBoxFacetExtension);
  const gp_Ax2 aPosition (aPos, aDir.Reversed());

  gp_Ax3 aSystem (aPosition);
  gp_Trsf aTrsf;
  aTrsf.SetTransformation (aSystem, gp_Ax3());

  return createRoundRectangleTriangles (gp_XY (mySize, mySize), myRoundRadius * mySize, aTrsf);
}

//=======================================================================
//function : createBoxEdgeTriangles
//purpose  :
//=======================================================================
Handle(Graphic3d_ArrayOfTriangles) AIS_ViewCube::createBoxEdgeTriangles (V3d_TypeOfOrientation theDirection) const
{
  const Standard_Real aThickness = Max (myBoxFacetExtension * gp_XY (1.0, 1.0).Modulus() - myBoxEdgeGap, myBoxEdgeMinSize);

  const gp_Dir aDir = V3d::GetProjAxis (theDirection);
  const gp_Pnt aPos = aDir.XYZ() * (mySize * 0.5 * gp_XY (1.0, 1.0).Modulus() + myBoxFacetExtension * Cos (M_PI_4));
  const gp_Ax2 aPosition (aPos, aDir.Reversed());

  gp_Ax3 aSystem (aPosition);
  gp_Trsf aTrsf;
  aTrsf.SetTransformation (aSystem, gp_Ax3());

  return createRoundRectangleTriangles (gp_XY (aThickness, mySize), myRoundRadius * mySize, aTrsf);
}

//=======================================================================
//function : createBoxCornerTriangles
//purpose  :
//=======================================================================
Handle(Graphic3d_ArrayOfTriangles) AIS_ViewCube::createBoxCornerTriangles (V3d_TypeOfOrientation theDir) const
{
  const Standard_Real aHSize = mySize * 0.5;
  const gp_Dir aDir = V3d::GetProjAxis (theDir);
  const gp_XYZ aHSizeDir = aDir.XYZ() * (aHSize * gp_Vec (1.0, 1.0, 1.0).Magnitude());
  if (myRoundRadius > 0.0)
  {
    const Standard_Real anEdgeHWidth = myBoxFacetExtension * gp_XY (1.0, 1.0).Modulus() * 0.5;
    const Standard_Real aHeight = anEdgeHWidth * Sqrt (2.0 / 3.0); // tetrahedron height
    const gp_Pnt aPos = aDir.XYZ() * (aHSize * gp_Vec (1.0, 1.0, 1.0).Magnitude() + aHeight);
    const gp_Ax2 aPosition (aPos, aDir.Reversed());
    gp_Ax3 aSystem (aPosition);
    gp_Trsf aTrsf;
    aTrsf.SetTransformation (aSystem, gp_Ax3());
    const Standard_Real aRadius = Max (myBoxFacetExtension * 0.5 / Cos (M_PI_4), myCornerMinSize);
    return Prs3d_ToolDisk::Create (0.0, aRadius, THE_NB_DISK_SLICES, 1, aTrsf);
  }

  Handle(Graphic3d_ArrayOfTriangles) aTris = new Graphic3d_ArrayOfTriangles (3, 3, Graphic3d_ArrayFlags_VertexNormal);

  aTris->AddVertex (aHSizeDir + myBoxFacetExtension * gp_Dir (aDir.X(), 0.0, 0.0).XYZ());
  aTris->AddVertex (aHSizeDir + myBoxFacetExtension * gp_Dir (0.0, aDir.Y(), 0.0).XYZ());
  aTris->AddVertex (aHSizeDir + myBoxFacetExtension * gp_Dir (0.0, 0.0, aDir.Z()).XYZ());

  const gp_XYZ aNode1 = aTris->Vertice (1).XYZ();
  const gp_XYZ aNode2 = aTris->Vertice (2).XYZ();
  const gp_XYZ aNode3 = aTris->Vertice (3).XYZ();
  const gp_XYZ aNormTri = ((aNode2 - aNode1).Crossed (aNode3 - aNode1));
  if (aNormTri.Dot (aDir.XYZ()) < 0.0)
  {
    aTris->AddEdges (1, 3, 2);
  }
  else
  {
    aTris->AddEdges (1, 2, 3);
  }

  for (Standard_Integer aVertIter = 1; aVertIter <= aTris->VertexNumber(); ++aVertIter)
  {
    aTris->SetVertexNormal (aVertIter, aDir);
  }
  return aTris;
}

//=======================================================================
//function : Compute
//purpose  :
//=======================================================================
void AIS_ViewCube::Compute (const Handle(PrsMgr_PresentationManager3d)& ,
                            const Handle(Prs3d_Presentation)& thePrs,
                            const Standard_Integer theMode)
{
  thePrs->SetInfiniteState (true);
  if (theMode != 0)
  {
    return;
  }

  const gp_Pnt aLocation = (mySize * 0.5 + myBoxFacetExtension + myAxesPadding) * gp_XYZ (-1.0, -1.0, -1.0);

  // Display axes
  if (myToDisplayAxes)
  {
    const Standard_Real anAxisSize = mySize + 2.0 * myBoxFacetExtension + myAxesPadding;
    const Handle(Prs3d_DatumAspect)& aDatumAspect = myDrawer->DatumAspect();
    for (Standard_Integer anAxisIter = Prs3d_DP_XAxis; anAxisIter <= Prs3d_DP_ZAxis; ++anAxisIter)
    {
      const Prs3d_DatumParts aPart = (Prs3d_DatumParts )anAxisIter;
      if (!aDatumAspect->DrawDatumPart (aPart))
      {
        continue;
      }

      gp_Ax1 anAx1;
      switch (aPart)
      {
        case Prs3d_DP_XAxis: anAx1 = gp_Ax1 (aLocation, gp::DX()); break;
        case Prs3d_DP_YAxis: anAx1 = gp_Ax1 (aLocation, gp::DY()); break;
        case Prs3d_DP_ZAxis: anAx1 = gp_Ax1 (aLocation, gp::DZ()); break;
        default: break;
      }

      Handle(Graphic3d_Group) anAxisGroup = thePrs->NewGroup();
      anAxisGroup->SetGroupPrimitivesAspect (aDatumAspect->ShadingAspect (aPart)->Aspect());

      const Standard_Real anArrowLength = 0.2 * anAxisSize;
      Handle(Graphic3d_ArrayOfTriangles) aTriangleArray = Prs3d_Arrow::DrawShaded (anAx1, 1.0, anAxisSize, 3.0, anArrowLength, THE_NB_ARROW_FACETTES);
      anAxisGroup->AddPrimitiveArray (aTriangleArray);

      TCollection_AsciiString anAxisLabel;
      if (aDatumAspect->ToDrawLabels()
      &&  myAxesLabels.Find (aPart, anAxisLabel)
      && !anAxisLabel.IsEmpty())
      {
        Handle(Graphic3d_Group) anAxisLabelGroup = thePrs->NewGroup();
        gp_Pnt aTextOrigin = anAx1.Location().Translated (gp_Vec (anAx1.Direction().X() * (anAxisSize + anArrowLength),
                                                                  anAx1.Direction().Y() * (anAxisSize + anArrowLength),
                                                                  anAx1.Direction().Z() * (anAxisSize + anArrowLength)));
        Prs3d_Text::Draw (anAxisLabelGroup, aDatumAspect->TextAspect(), TCollection_ExtendedString (anAxisLabel), aTextOrigin);
      }
    }

    // Display center
    {
      Handle(Graphic3d_Group) aGroup = thePrs->NewGroup();
      Handle(Prs3d_ShadingAspect) anAspectCen = new Prs3d_ShadingAspect();
      anAspectCen->SetColor (Quantity_NOC_WHITE);
      aGroup->SetGroupPrimitivesAspect (anAspectCen->Aspect());
      Prs3d_ToolSphere aTool (4.0, THE_NB_DISK_SLICES, THE_NB_DISK_SLICES);
      gp_Trsf aTrsf;
      aTrsf.SetTranslation (gp_Vec (gp::Origin(), aLocation));
      Handle(Graphic3d_ArrayOfTriangles) aCenterArray;
      aTool.FillArray (aCenterArray, aTrsf);
      aGroup->AddPrimitiveArray (aCenterArray);
    }
  }

  // Display box
  {
    Handle(Graphic3d_Group) aGroupSides = thePrs->NewGroup(), aGroupEdges = thePrs->NewGroup(), aGroupCorners = thePrs->NewGroup();
    aGroupSides->SetClosed (true); // should be replaced by forced back-face culling aspect
    aGroupSides->SetGroupPrimitivesAspect (myDrawer->ShadingAspect()->Aspect());

    aGroupEdges->SetClosed (true);
    aGroupEdges->SetGroupPrimitivesAspect (myBoxEdgeAspect->Aspect());

    aGroupCorners->SetClosed (true);
    aGroupCorners->SetGroupPrimitivesAspect (myBoxCornerAspect->Aspect());

    Handle(Graphic3d_Group) aTextGroup = thePrs->NewGroup();
    //aTextGroup->SetClosed (true);
    aTextGroup->SetGroupPrimitivesAspect (myDrawer->TextAspect()->Aspect());
    for (Standard_Integer aPartIter = 0; aPartIter <= Standard_Integer(V3d_XnegYnegZneg); ++aPartIter)
    {
      const V3d_TypeOfOrientation anOrient = (V3d_TypeOfOrientation )aPartIter;
      if (Handle(Graphic3d_ArrayOfTriangles) aTris = createBoxPartTriangles (anOrient))
      {
        if (IsBoxSide (anOrient))
        {
          aGroupSides->AddPrimitiveArray (aTris);

          TCollection_AsciiString aLabel;
          if (!myBoxSideLabels.Find (anOrient, aLabel)
            || aLabel.IsEmpty())
          {
            continue;
          }

          const gp_Dir aDir = V3d::GetProjAxis (anOrient);
          gp_Dir anUp = myIsYup ? gp::DY() : gp::DZ();
          if (myIsYup)
          {
            if (anOrient == V3d_Ypos
             || anOrient == V3d_Yneg)
            {
              anUp = -gp::DZ();
            }
          }
          else
          {
            if (anOrient == V3d_Zpos)
            {
              anUp = gp::DY();
            }
            else if (anOrient == V3d_Zneg)
            {
              anUp = -gp::DY();
            }
          }

          const Standard_Real anOffset = 2.0; // extra offset to avoid overlapping with triangulation
          const gp_Pnt aPos = aDir.XYZ() * (mySize * 0.5 + myBoxFacetExtension + anOffset);
          const gp_Ax2 aPosition (aPos, aDir, anUp.Crossed (aDir));
          Prs3d_Text::Draw (aTextGroup, myDrawer->TextAspect(), aLabel, aPosition);
        }
        else if (IsBoxEdge (anOrient))
        {
          aGroupEdges->AddPrimitiveArray (aTris);
        }
        else if (IsBoxCorner (anOrient))
        {
          aGroupCorners->AddPrimitiveArray (aTris);
        }
      }
    }
  }
}

//=======================================================================
//function : ComputeSelection
//purpose  :
//=======================================================================
void AIS_ViewCube::ComputeSelection (const Handle(SelectMgr_Selection)& theSelection,
                                     const Standard_Integer theMode)
{
  if (theMode != 0)
  {
    return;
  }

  for (Standard_Integer aPartIter = 0; aPartIter <= Standard_Integer(V3d_XnegYnegZneg); ++aPartIter)
  {
    const V3d_TypeOfOrientation anOri = (V3d_TypeOfOrientation )aPartIter;
    if (Handle(Graphic3d_ArrayOfTriangles) aTris = createBoxPartTriangles (anOri))
    {
      Standard_Integer aSensitivity = 2;
      if (IsBoxCorner (anOri))
      {
        aSensitivity = 8;
      }
      else if (IsBoxEdge (anOri))
      {
        aSensitivity = 4;
      }
      Handle(AIS_ViewCubeOwner) anOwner = new AIS_ViewCubeOwner (this, anOri);
      Handle(AIS_ViewCubeSensitive) aTriSens = new AIS_ViewCubeSensitive (anOwner, aTris);
      aTriSens->SetSensitivityFactor (aSensitivity);
      theSelection->Add (aTriSens);
    }
  }
}

//=======================================================================
//function : HasAnimation
//purpose  :
//=======================================================================
Standard_Boolean AIS_ViewCube::HasAnimation() const
{
  return !myViewAnimation->IsStopped();
}

//=======================================================================
//function : StartAnimation
//purpose  :
//=======================================================================
void AIS_ViewCube::StartAnimation (const Handle(AIS_ViewCubeOwner)& theOwner)
{
  Handle(V3d_View) aView = GetContext()->LastActiveView();
  if (theOwner.IsNull()
   || aView.IsNull())
  {
    return;
  }

  myStartState->Copy (aView->Camera());
  myEndState  ->Copy (aView->Camera());

  {
    Handle(Graphic3d_Camera) aBackupCamera = new Graphic3d_Camera (aView->Camera());

    const bool wasImmediateUpdate = aView->SetImmediateUpdate (false);
    aView->SetCamera (myEndState);
    aView->SetProj (theOwner->MainOrientation(), myIsYup);

    const gp_Dir aNewDir = aView->Camera()->Direction();
    if (!myToResetCameraUp
     && !aNewDir.IsEqual (aBackupCamera->Direction(), Precision::Angular()))
    {
      // find the Up direction closest to current instead of default one
      const gp_Ax1 aNewDirAx1 (gp::Origin(), aNewDir);
      const gp_Dir anOldUp = aBackupCamera->Up();
      const gp_Dir anUpList[4] =
      {
        aView->Camera()->Up(),
        aView->Camera()->Up().Rotated (aNewDirAx1, M_PI_2),
        aView->Camera()->Up().Rotated (aNewDirAx1, M_PI),
        aView->Camera()->Up().Rotated (aNewDirAx1, M_PI * 1.5),
      };

      Standard_Real aBestAngle = Precision::Infinite();
      gp_Dir anUpBest;
      for (Standard_Integer anUpIter = 0; anUpIter < 4; ++anUpIter)
      {
        Standard_Real anAngle = anUpList[anUpIter].Angle (anOldUp);
        if (aBestAngle > anAngle)
        {
          aBestAngle = anAngle;
          anUpBest = anUpList[anUpIter];
        }
      }
      aView->Camera()->SetUp (anUpBest);
    }

    const Bnd_Box aBndSelected = myToFitSelected ? GetContext()->BoundingBoxOfSelection() : Bnd_Box();
    if (!aBndSelected.IsVoid())
    {
      aView->FitAll (aBndSelected, 0.01, false);
    }
    else
    {
      aView->FitAll (0.01, false);
    }
    aView->SetCamera (aBackupCamera);
    aView->SetImmediateUpdate (wasImmediateUpdate);
  }

  myViewAnimation->SetView (aView);
  myViewAnimation->SetCameraStart (myStartState);
  myViewAnimation->SetCameraEnd   (myEndState);
  myViewAnimation->SetOwnDuration (myDuration);
  myViewAnimation->StartTimer (0.0, 1.0, true, false);
}

//=======================================================================
//function : updateAnimation
//purpose  :
//=======================================================================
Standard_Boolean AIS_ViewCube::updateAnimation()
{
  const Standard_Real aPts = myViewAnimation->UpdateTimer();
  if (aPts >= myDuration)
  {
    myViewAnimation->Stop();
    onAnimationFinished();
    myViewAnimation->SetView (Handle(V3d_View)());
    return Standard_False;
  }
  return Standard_True;
}

//=======================================================================
//function : UpdateAnimation
//purpose  :
//=======================================================================
Standard_Boolean AIS_ViewCube::UpdateAnimation (const Standard_Boolean theToUpdate)
{
  Handle(V3d_View) aView = myViewAnimation->View();
  if (!HasAnimation()
   || !updateAnimation())
  {
    return Standard_False;
  }

  if (theToUpdate
  && !aView.IsNull())
  {
    aView->IsInvalidated() ? aView->Redraw() : aView->RedrawImmediate();
  }

  onAfterAnimation();
  return Standard_True;
}

//=======================================================================
//function : HandleClick
//purpose  :
//=======================================================================
void AIS_ViewCube::HandleClick (const Handle(AIS_ViewCubeOwner)& theOwner)
{
  if (!myToAutoStartAnim)
  {
    return;
  }

  StartAnimation (theOwner);
  if (!myIsFixedAnimation)
  {
    return;
  }
  for (; HasAnimation(); )
  {
    UpdateAnimation (true);
  }
}

//=======================================================================
//function : HilightOwnerWithColor
//purpose  :
//=======================================================================
void AIS_ViewCube::HilightOwnerWithColor (const Handle(PrsMgr_PresentationManager3d)& thePrsMgr,
                                          const Handle(Prs3d_Drawer)& theStyle,
                                          const Handle(SelectMgr_EntityOwner)& theOwner)
{
  if (theOwner.IsNull()
  || !thePrsMgr->IsImmediateModeOn())
  {
    return;
  }

  const Graphic3d_ZLayerId aLayer = theStyle->ZLayer() != Graphic3d_ZLayerId_UNKNOWN ? theStyle->ZLayer() : myDrawer->ZLayer();
  const AIS_ViewCubeOwner* aCubeOwner = dynamic_cast<AIS_ViewCubeOwner* >(theOwner.get());

  Handle(Prs3d_Presentation) aHiPrs = GetHilightPresentation (thePrsMgr);
  aHiPrs->Clear();
  aHiPrs->CStructure()->ViewAffinity = thePrsMgr->StructureManager()->ObjectAffinity (Handle(Standard_Transient)(this));
  aHiPrs->SetTransformPersistence (TransformPersistence());
  aHiPrs->SetZLayer (aLayer);

  {
    Handle(Graphic3d_Group) aGroup = aHiPrs->NewGroup();
    aGroup->SetGroupPrimitivesAspect (theStyle->ShadingAspect()->Aspect());
    if (Handle(Graphic3d_ArrayOfTriangles) aTris = createBoxPartTriangles (aCubeOwner->MainOrientation()))
    {
      aGroup->AddPrimitiveArray (aTris);
    }
  }

  if (thePrsMgr->IsImmediateModeOn())
  {
    thePrsMgr->AddToImmediateList (aHiPrs);
  }
}

//=======================================================================
//function : HilightSelected
//purpose  :
//=======================================================================
void AIS_ViewCube::HilightSelected (const Handle(PrsMgr_PresentationManager3d)& ,
                                    const SelectMgr_SequenceOfOwner& theSeq)
{
  // this method should never be called since AIS_InteractiveObject::HandleClick() has been overridden
  if (theSeq.Size() == 1)
  {
    //HandleClick (Handle(AIS_ViewCubeOwner)::DownCast (theSeq.First()));
  }
}
