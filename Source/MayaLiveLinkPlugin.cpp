// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RequiredProgramMainCPPInclude.h"
#include "Misc/CommandLine.h"
#include "Async/TaskGraphInterfaces.h"
#include "Modules/ModuleManager.h"
#include "UObject/Object.h"
#include "Misc/ConfigCacheIni.h"

#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "Roles/LiveLinkLightRole.h"
#include "Roles/LiveLinkLightTypes.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "LiveLinkProvider.h"
#include "LiveLinkRefSkeleton.h"
#include "LiveLinkTypes.h"
#include "Misc/OutputDevice.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlankMayaPlugin, Log, All);

IMPLEMENT_APPLICATION(MayaLiveLinkPlugin, "MayaLiveLinkPlugin");

// Maya includes
// For Maya 2016 the SDK has to be downloaded and installed manually for these includes to work.
#ifndef BananaFritters
#define BananaFritters unsigned int
#endif
#define DWORD BananaFritters
#include <maya/MObject.h>
#include <maya/MGlobal.h>
#include <maya/MFnPlugin.h>
#include <maya/MPxCommand.h> //command
#include <maya/MCommandResult.h> //command
#include <maya/MPxNode.h> //node
#include <maya/MFnNumericAttribute.h>
#include <maya/MCallbackIdArray.h>
#include <maya/MEventMessage.h>
#include <maya/MDagMessage.h>
#include <maya/MItDag.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MMatrix.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MQuaternion.h>
#include <maya/MStringArray.h>
#include <maya/MString.h>
#include <maya/MVector.h>
#include <maya/MFnTransform.h>
#include <maya/MFnIkJoint.h>
#include <maya/MFnCamera.h>
#include <maya/MFnLight.h>
#include <maya/MFnSpotLight.h>
#include <maya/MEulerRotation.h>
#include <maya/MSelectionList.h>
#include <maya/MAnimControl.h>
#include <maya/MTimerMessage.h>
#include <maya/MDGMessage.h>
#include <maya/MNodeMessage.h>
#include <maya/MSceneMessage.h>
#include <maya/M3dView.h>
#include <maya/MUiMessage.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>

#undef DWORD

#define MCHECKERROR(STAT,MSG)                   \
    if (!STAT) {                                \
        perror(MSG);                            \
        return MS::kFailure;                    \
    }

#define MREPORTERROR(STAT,MSG)                  \
    if (!STAT) {                                \
        perror(MSG);                            \
    }

class FLiveLinkStreamedSubjectManager;

TSharedPtr<ILiveLinkProvider> LiveLinkProvider;
TSharedPtr<FLiveLinkStreamedSubjectManager> LiveLinkStreamManager;
FDelegateHandle ConnectionStatusChangedHandle;

MCallbackIdArray myCallbackIds;

MSpace::Space G_TransformSpace = MSpace::kTransform;

bool bUEInitialized = false;

// Execute the python command to refresh our UI
void RefreshUI()
{
	MGlobal::executeCommand("MayaLiveLinkRefreshUI");
}

void SetMatrixRow(double* Row, MVector Vec)
{
	Row[0] = Vec.x;
	Row[1] = Vec.y;
	Row[2] = Vec.z;
}

double RadToDeg(double Rad)
{
	const double E_PI = 3.1415926535897932384626433832795028841971693993751058209749445923078164062;
	return (Rad*180.0) / E_PI;
}

double DegToRad(double Deg)
{
	const double E_PI = 3.1415926535897932384626433832795028841971693993751058209749445923078164062;
	return Deg * (E_PI / 180.0f);
}

MMatrix GetScale(const MFnIkJoint& Joint)
{
	double Scale[3];
	Joint.getScale(Scale);
	MTransformationMatrix M;
	M.setScale(Scale, G_TransformSpace);
	return M.asMatrix();
}

MMatrix GetRotationOrientation(const MFnIkJoint& Joint, MTransformationMatrix::RotationOrder& RotOrder)
{
	double ScaleOrientation[3];
	Joint.getScaleOrientation(ScaleOrientation, RotOrder);
	MTransformationMatrix M;
	M.setRotation(ScaleOrientation, RotOrder);
	return M.asMatrix();
}

MMatrix GetRotation(const MFnIkJoint& Joint, MTransformationMatrix::RotationOrder& RotOrder)
{
	double Rotation[3];
	Joint.getRotation(Rotation, RotOrder);
	MTransformationMatrix M;
	M.setRotation(Rotation, RotOrder);
	return M.asMatrix();
}

MMatrix GetJointOrientation(const MFnIkJoint& Joint, MTransformationMatrix::RotationOrder& RotOrder)
{
	double JointOrientation[3];
	Joint.getOrientation(JointOrientation, RotOrder);
	MTransformationMatrix M;
	M.setRotation(JointOrientation, RotOrder);
	return M.asMatrix();
}

MMatrix GetTranslation(const MFnIkJoint& Joint)
{
	MVector Translation = Joint.getTranslation(G_TransformSpace);
	MTransformationMatrix M;
	M.setTranslation(Translation, G_TransformSpace);
	return M.asMatrix();
}

void RotateCoordinateSystemForUnreal(MMatrix& InOutMatrix)
{
	MQuaternion RotOffset;
	RotOffset.setToXAxis(DegToRad(90.0));
	InOutMatrix *= RotOffset.asMatrix();
}

FTransform BuildUETransformFromMayaTransform(MMatrix& InMatrix)
{
	MMatrix UnrealSpaceJointMatrix;

	// from FFbxDataConverter::ConvertMatrix
	for (int i = 0; i < 4; ++i)
	{
		double* Row = InMatrix[i];
		if (i == 1)
		{
			UnrealSpaceJointMatrix[i][0] = -Row[0];
			UnrealSpaceJointMatrix[i][1] = Row[1];
			UnrealSpaceJointMatrix[i][2] = -Row[2];
			UnrealSpaceJointMatrix[i][3] = -Row[3];
		}
		else
		{
			UnrealSpaceJointMatrix[i][0] = Row[0];
			UnrealSpaceJointMatrix[i][1] = -Row[1];
			UnrealSpaceJointMatrix[i][2] = Row[2];
			UnrealSpaceJointMatrix[i][3] = Row[3];
		}
	}

	//OutputRotation(FinalJointMatrix);

	MTransformationMatrix UnrealSpaceJointTransform(UnrealSpaceJointMatrix);

	// getRotation is MSpace::kTransform
	double tx, ty, tz, tw;
	UnrealSpaceJointTransform.getRotationQuaternion(tx, ty, tz, tw, MSpace::kWorld);

	FTransform UETrans;
	UETrans.SetRotation(FQuat(tx, ty, tz, tw));

	MVector Translation = UnrealSpaceJointTransform.getTranslation(MSpace::kWorld);
	UETrans.SetTranslation(FVector(Translation.x, Translation.y, Translation.z));

	double Scale[3];
	UnrealSpaceJointTransform.getScale(Scale, MSpace::kWorld);
	UETrans.SetScale3D(FVector((float)Scale[0], (float)Scale[1], (float)Scale[2]));
	return UETrans;
}

FColor MayaColorToUnreal(MColor Color)
{
	FColor Result;
	Result.R = FMath::Clamp(Color[0] * 255.0, 0.0, 255.0);
	Result.G = FMath::Clamp(Color[1] * 255.0, 0.0, 255.0);
	Result.B = FMath::Clamp(Color[2] * 255.0, 0.0, 255.0);
	Result.A = 255;
	return Result;
}

void OutputRotation(const MMatrix& M)
{
	MTransformationMatrix TM(M);

	MEulerRotation Euler = TM.eulerRotation();

	FVector V;

	V.X = RadToDeg(Euler[0]);
	V.Y = RadToDeg(Euler[1]);
	V.Z = RadToDeg(Euler[2]);
	MGlobal::displayInfo(*V.ToString());
}

struct IStreamedEntity
{
public:
	virtual ~IStreamedEntity() {};

	virtual bool ShouldDisplayInUI() const { return false; }
	virtual MDagPath GetDagPath() const = 0;
	virtual MString GetNameDisplayText() const = 0;
	virtual MString GetRoleDisplayText() const = 0;
	virtual MString GetSubjectTypeDisplayText() const = 0;
	virtual bool ValidateSubject() const = 0;
	virtual void RebuildSubjectData() = 0;
	virtual void OnStream(double StreamTime, int32 FrameNumber) = 0;
	virtual void SetStreamType(const FString& StreamType) = 0;
};

struct FStreamHierarchy
{
	FName JointName;
	MFnIkJoint JointObject;
	int32 ParentIndex;

	FStreamHierarchy() {}

	FStreamHierarchy(const FStreamHierarchy& Other)
		: JointName(Other.JointName)
		, JointObject(Other.JointObject.dagPath())
		, ParentIndex(Other.ParentIndex)
	{}

	FStreamHierarchy(FName InJointName, const MDagPath& InJointPath, int32 InParentIndex)
		: JointName(InJointName)
		, JointObject(InJointPath)
		, ParentIndex(InParentIndex)
	{}
};

struct FLiveLinkStreamedJointHierarchySubject : IStreamedEntity
{
	FLiveLinkStreamedJointHierarchySubject(FName InSubjectName, MDagPath InRootPath)
		: SubjectName(InSubjectName)
		, RootDagPath(InRootPath)
		, StreamMode(FCharacterStreamMode::FullHierarchy)
	{}

	~FLiveLinkStreamedJointHierarchySubject()
	{
		if (LiveLinkProvider.IsValid())
		{
			LiveLinkProvider->RemoveSubject(SubjectName);
		}
	}

	virtual bool ShouldDisplayInUI() const override { return true; }
	virtual MDagPath GetDagPath() const override { return RootDagPath; }
	virtual MString GetNameDisplayText() const override { return MString(*SubjectName.ToString()); }
	virtual MString GetRoleDisplayText() const override { return MString(*(CharacterStreamOptions[StreamMode])); }
	virtual MString GetSubjectTypeDisplayText() const override { return MString("Character"); }

	virtual bool ValidateSubject() const override
	{
		MStatus Status;
		bool bIsValid = RootDagPath.isValid(&Status);

		TCHAR* StatusMessage = TEXT("Unset");

		if (Status == MS::kSuccess)
		{
			StatusMessage = TEXT("Success");
		}
		else if (Status == MS::kFailure)
		{
			StatusMessage = TEXT("Failure");
		}
		else
		{
			StatusMessage = TEXT("Other");
		}

		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Testing %s for removal Path:%s Valid:%s Status:%s\n"), *SubjectName.ToString(), RootDagPath.fullPathName().asWChar(), bIsValid ? TEXT("true") : TEXT("false"), StatusMessage);
		if (Status != MS::kFailure && bIsValid)
		{
			//Path checks out as valid
			MFnIkJoint Joint(RootDagPath, &Status);

			MVector returnvec = Joint.getTranslation(MSpace::kWorld, &Status);
			if (Status == MS::kSuccess)
			{
				StatusMessage = TEXT("Success");
			}
			else if (Status == MS::kFailure)
			{
				StatusMessage = TEXT("Failure");
			}
			else
			{
				StatusMessage = TEXT("Other");
			}

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\tTesting %s for removal Path:%s Valid:%s Status:%s\n"), *SubjectName.ToString(), RootDagPath.fullPathName().asWChar(), bIsValid ? TEXT("true") : TEXT("false"), StatusMessage);
		}
		return bIsValid;
	}

	virtual void RebuildSubjectData() override
	{
		if (StreamMode == FCharacterStreamMode::RootOnly)
		{
			FLiveLinkStaticDataStruct StaticData(FLiveLinkTransformStaticData::StaticStruct());
			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkTransformRole::StaticClass(), MoveTemp(StaticData));
		}
		else if (StreamMode == FCharacterStreamMode::FullHierarchy)
		{
			JointsToStream.Reset();

			FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
			FLiveLinkSkeletonStaticData& AnimationData = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

			MItDag::TraversalType traversalType = MItDag::kBreadthFirst;
			MFn::Type filter = MFn::kJoint;

			MStatus status;
			MItDag JointIterator;
			JointIterator.reset(RootDagPath, MItDag::kDepthFirst, MFn::kJoint);

			//Build Hierarchy
			TArray<int32> ParentIndexStack;
			ParentIndexStack.SetNum(100, false);

			int32 Index = 0;

			for (; !JointIterator.isDone(); JointIterator.next())
			{
				uint32 Depth = JointIterator.depth();
				if (Depth >= (uint32)ParentIndexStack.Num())
				{
					ParentIndexStack.SetNum(Depth + 1);
				}
				ParentIndexStack[Depth] = Index++;

				int32 ParentIndex = Depth == 0 ? -1 : ParentIndexStack[Depth - 1];

				MDagPath JointPath;
				status = JointIterator.getPath(JointPath);
				MFnIkJoint JointObject(JointPath);

				FName JointName(JointObject.name().asChar());

				JointsToStream.Add(FStreamHierarchy(JointName, JointPath, ParentIndex));
				AnimationData.BoneNames.Add(JointName);
				AnimationData.BoneParents.Add(ParentIndex);
			}

			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
		}
	}

	virtual void OnStream(double StreamTime, int32 FrameNumber) override
	{
		if (StreamMode == FCharacterStreamMode::RootOnly)
		{
			MFnTransform TransformNode(RootDagPath);

			MMatrix Transform = TransformNode.transformation().asMatrix();

			FLiveLinkFrameDataStruct FrameData(FLiveLinkTransformFrameData::StaticStruct());
			FLiveLinkTransformFrameData& TransformData = *FrameData.Cast<FLiveLinkTransformFrameData>();

			RotateCoordinateSystemForUnreal(Transform);

			// Convert Maya Camera orientation to Unreal
			TransformData.Transform = BuildUETransformFromMayaTransform(Transform);

			TransformData.WorldTime = StreamTime;
			LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
		}
		else if (StreamMode == FCharacterStreamMode::FullHierarchy)
		{
			FLiveLinkFrameDataStruct FrameData(FLiveLinkAnimationFrameData::StaticStruct());
			FLiveLinkAnimationFrameData& AnimationData = *FrameData.Cast<FLiveLinkAnimationFrameData>();

			AnimationData.Transforms.Reserve(JointsToStream.Num());

			TArray<MMatrix> InverseScales;
			InverseScales.Reserve(JointsToStream.Num());

			for (int32 Idx = 0; Idx < JointsToStream.Num(); ++Idx)
			{
				const FStreamHierarchy& H = JointsToStream[Idx];

				MTransformationMatrix::RotationOrder RotOrder = H.JointObject.rotationOrder();

				MMatrix JointScale = GetScale(H.JointObject);
				InverseScales.Add(JointScale.inverse());

				MMatrix ParentInverseScale = (H.ParentIndex == -1) ? MMatrix::identity : InverseScales[H.ParentIndex];

				MMatrix MayaSpaceJointMatrix = JointScale *
					GetRotationOrientation(H.JointObject, RotOrder) *
					GetRotation(H.JointObject, RotOrder) *
					GetJointOrientation(H.JointObject, RotOrder) *
					ParentInverseScale *
					GetTranslation(H.JointObject);

				if (Idx == 0) // rotate the root joint to get the correct character rotation in Unreal
				{
					RotateCoordinateSystemForUnreal(MayaSpaceJointMatrix);
				}

				AnimationData.Transforms.Add(BuildUETransformFromMayaTransform(MayaSpaceJointMatrix));
			}

			AnimationData.WorldTime = StreamTime;
			LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
		}
	}

	virtual void SetStreamType(const FString& StreamTypeIn) override
	{
		for (int32 StreamTypeIdx = 0; StreamTypeIdx < CharacterStreamOptions.Num(); ++StreamTypeIdx)
		{
			if (CharacterStreamOptions[StreamTypeIdx] == StreamTypeIn && StreamMode != (FCharacterStreamMode)StreamTypeIdx)
			{
				StreamMode = (FCharacterStreamMode)StreamTypeIdx;
				RebuildSubjectData();
				return;
			}
		}
	}

private:
	FName SubjectName;
	MDagPath RootDagPath;
	TArray<FStreamHierarchy> JointsToStream;

	const TArray<FString> CharacterStreamOptions = { TEXT("Root Only"), TEXT("Full Hierarchy") };
	enum FCharacterStreamMode
	{
		RootOnly,
		FullHierarchy,
	};
	FCharacterStreamMode StreamMode;
};

struct FLiveLinkBaseCameraStreamedSubject : public IStreamedEntity
{
public:
	FLiveLinkBaseCameraStreamedSubject(FName InSubjectName) : SubjectName(InSubjectName), StreamMode(FCameraStreamMode::Camera) {}

	~FLiveLinkBaseCameraStreamedSubject()
	{
		if (LiveLinkProvider.IsValid())
		{
			LiveLinkProvider->RemoveSubject(SubjectName);
		}
	}

	virtual MString GetNameDisplayText() const override { return *SubjectName.ToString(); }
	virtual MString GetRoleDisplayText() const override { return MString(*(CameraStreamOptions[StreamMode])); }
	virtual MString GetSubjectTypeDisplayText() const override { return MString("Camera"); }

	virtual bool ValidateSubject() const override { return true; }

	virtual void RebuildSubjectData() override
	{
		if (StreamMode == FCameraStreamMode::RootOnly)
		{
			FLiveLinkStaticDataStruct TransformData(FLiveLinkTransformStaticData::StaticStruct());
			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkTransformRole::StaticClass(), MoveTemp(TransformData));
		}
		else if (StreamMode == FCameraStreamMode::FullHierarchy)
		{
			FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
			FLiveLinkSkeletonStaticData& AnimationData = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

			AnimationData.BoneNames.Add(FName("root"));
			AnimationData.BoneParents.Add(-1);

			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
		}
		else if (StreamMode == FCameraStreamMode::Camera)
		{
			FLiveLinkStaticDataStruct StaticData(FLiveLinkCameraStaticData::StaticStruct());
			FLiveLinkCameraStaticData& CameraData = *StaticData.Cast<FLiveLinkCameraStaticData>();
			CameraData.bIsFieldOfViewSupported = true;
			CameraData.bIsAspectRatioSupported = true;
			CameraData.bIsFocalLengthSupported = true;
			CameraData.bIsProjectionModeSupported = true;
			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkCameraRole::StaticClass(), MoveTemp(StaticData));
		}
	}

	void StreamCamera(MDagPath CameraPath, double StreamTime, int32 FrameNumber)
	{
		MStatus Status;
		bool bIsValid = CameraPath.isValid(&Status);

		if (bIsValid && Status == MStatus::kSuccess)
		{
			MFnCamera C(CameraPath);

			MPoint EyeLocation = C.eyePoint(MSpace::kWorld);

			MMatrix CameraTransformMatrix;
			SetMatrixRow(CameraTransformMatrix[0], C.rightDirection(MSpace::kWorld));
			SetMatrixRow(CameraTransformMatrix[1], C.viewDirection(MSpace::kWorld));
			SetMatrixRow(CameraTransformMatrix[2], C.upDirection(MSpace::kWorld));
			SetMatrixRow(CameraTransformMatrix[3], EyeLocation);

			RotateCoordinateSystemForUnreal(CameraTransformMatrix);
			FTransform CameraTransform = BuildUETransformFromMayaTransform(CameraTransformMatrix);
			// Convert Maya Camera orientation to Unreal
			CameraTransform.SetRotation(CameraTransform.GetRotation() * FRotator(0.0f, -90.0f, 0.0f).Quaternion());

			if (StreamMode == FCameraStreamMode::RootOnly)
			{
				FLiveLinkFrameDataStruct TransformData = (FLiveLinkTransformFrameData::StaticStruct());
				FLiveLinkTransformFrameData& CameraTransformData = *TransformData.Cast<FLiveLinkTransformFrameData>();
				CameraTransformData.Transform = CameraTransform;
				CameraTransformData.WorldTime = StreamTime;
				LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(TransformData));
			}
			else if (StreamMode == FCameraStreamMode::FullHierarchy)
			{
				FLiveLinkFrameDataStruct FrameData(FLiveLinkAnimationFrameData::StaticStruct());
				FLiveLinkAnimationFrameData& AnimationData = *FrameData.Cast<FLiveLinkAnimationFrameData>();

				AnimationData.Transforms.Add(CameraTransform);
				AnimationData.WorldTime = StreamTime;
				LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
			}
			else if (StreamMode == FCameraStreamMode::Camera)
			{
				FLiveLinkFrameDataStruct FrameData(FLiveLinkCameraFrameData::StaticStruct());
				FLiveLinkCameraFrameData& CameraData = *FrameData.Cast<FLiveLinkCameraFrameData>();

				CameraData.FieldOfView = C.horizontalFieldOfView();
				CameraData.AspectRatio = C.aspectRatio();
				CameraData.FocalLength = C.focalLength();
				CameraData.ProjectionMode = C.isOrtho() ? ELiveLinkCameraProjectionMode::Orthographic : ELiveLinkCameraProjectionMode::Perspective;

				CameraData.Transform = CameraTransform;
				CameraData.WorldTime = StreamTime;

				LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
			}
		}
	}

	virtual void SetStreamType(const FString& StreamTypeIn) override
	{
		for (int32 StreamTypeIdx = 0; StreamTypeIdx < CameraStreamOptions.Num(); ++StreamTypeIdx)
		{
			if (CameraStreamOptions[StreamTypeIdx] == StreamTypeIn && StreamMode != (FCameraStreamMode)StreamTypeIdx)
			{
				StreamMode = (FCameraStreamMode)StreamTypeIdx;
				RebuildSubjectData();
				return;
			}
		}
	}

protected:
	FName  SubjectName;

	const TArray<FString> CameraStreamOptions = { TEXT("Root Only"), TEXT("Full Hierarchy"), TEXT("Camera") };
	enum FCameraStreamMode
	{
		RootOnly,
		FullHierarchy,
		Camera,
	};
	FCameraStreamMode StreamMode;
};

struct FLiveLinkStreamedActiveCamera : public FLiveLinkBaseCameraStreamedSubject
{
public:
	FLiveLinkStreamedActiveCamera() : FLiveLinkBaseCameraStreamedSubject(ActiveCameraName) {}

	MDagPath CurrentActiveCameraDag;

	virtual MDagPath GetDagPath() const override { return CurrentActiveCameraDag; }

	virtual void OnStream(double StreamTime, int32 FrameNumber) override
	{
		MStatus Status;
		M3dView ActiveView = M3dView::active3dView(&Status);
		if (Status == MStatus::kSuccess)
		{
			MDagPath CameraDag;
			if (ActiveView.getCamera(CameraDag) == MStatus::kSuccess)
			{
				CurrentActiveCameraDag = CameraDag;
			}
		}

		StreamCamera(CurrentActiveCameraDag, StreamTime, FrameNumber);
	}

private:
	static FName ActiveCameraName;
};

FName FLiveLinkStreamedActiveCamera::ActiveCameraName("EditorActiveCamera");

struct FLiveLinkStreamedCameraSubject : FLiveLinkBaseCameraStreamedSubject
{
public:
	virtual MDagPath GetDagPath() const override { return CameraPath; }

	FLiveLinkStreamedCameraSubject(FName InSubjectName, MDagPath InDagPath) : FLiveLinkBaseCameraStreamedSubject(InSubjectName), CameraPath(InDagPath) {}

	virtual bool ShouldDisplayInUI() const override { return true; }

	virtual void OnStream(double StreamTime, int32 FrameNumber) override
	{
		StreamCamera(CameraPath, StreamTime, FrameNumber);
	}

private:
	MDagPath CameraPath;
};

struct FLiveLinkStreamedLightSubject : IStreamedEntity
{
public:
	FLiveLinkStreamedLightSubject(FName InSubjectName, MDagPath InRootPath)
		: SubjectName(InSubjectName)
		, RootDagPath(InRootPath)
		, StreamMode(FLightStreamMode::Light)
	{}

	~FLiveLinkStreamedLightSubject()
	{
		if (LiveLinkProvider.IsValid())
		{
			LiveLinkProvider->RemoveSubject(SubjectName);
		}
	}

	virtual bool ShouldDisplayInUI() const override { return true; }
	virtual MDagPath GetDagPath() const override { return RootDagPath; }
	virtual MString GetNameDisplayText() const override { return MString(*SubjectName.ToString()); }
	virtual MString GetRoleDisplayText() const override { return MString(*(LightStreamOptions[StreamMode])); }
	virtual MString GetSubjectTypeDisplayText() const override { return MString("Light"); }

	virtual bool ValidateSubject() const override { return true; }

	virtual void RebuildSubjectData() override
	{
		if (StreamMode == FLightStreamMode::RootOnly)
		{
			FLiveLinkStaticDataStruct TransformData(FLiveLinkTransformStaticData::StaticStruct());
			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkTransformRole::StaticClass(), MoveTemp(TransformData));
		}
		else if (StreamMode == FLightStreamMode::FullHierarchy)
		{
			FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
			FLiveLinkSkeletonStaticData& AnimationData = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

			AnimationData.BoneNames.Add(FName("root"));
			AnimationData.BoneParents.Add(-1);

			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
		}
		else if (StreamMode == FLightStreamMode::Light)
		{
			FLiveLinkStaticDataStruct StaticData(FLiveLinkLightStaticData::StaticStruct());
			FLiveLinkLightStaticData& LightData = *StaticData.Cast<FLiveLinkLightStaticData>();
			LightData.bIsIntensitySupported = true;
			LightData.bIsLightColorSupported = true;
			const bool bIsSpotLight = RootDagPath.hasFn(MFn::kSpotLight);
			LightData.bIsInnerConeAngleSupported = bIsSpotLight;
			LightData.bIsOuterConeAngleSupported = bIsSpotLight;

			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkLightRole::StaticClass(), MoveTemp(StaticData));
		}
	}

	virtual void OnStream(double StreamTime, int32 FrameNumber) override
	{
		MFnTransform TransformNode(RootDagPath);
		MMatrix MayaTransform = TransformNode.transformation().asMatrix();
		RotateCoordinateSystemForUnreal(MayaTransform);
		const FTransform UnrealTransform = BuildUETransformFromMayaTransform(MayaTransform);

		if (StreamMode == FLightStreamMode::RootOnly)
		{
			FLiveLinkFrameDataStruct FrameData(FLiveLinkTransformFrameData::StaticStruct());
			FLiveLinkTransformFrameData& TransformData = *FrameData.Cast<FLiveLinkTransformFrameData>();

			TransformData.Transform = UnrealTransform;
			TransformData.WorldTime = StreamTime;
			LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
		}
		else if (StreamMode == FLightStreamMode::FullHierarchy)
		{
			FLiveLinkFrameDataStruct FrameData(FLiveLinkAnimationFrameData::StaticStruct());
			FLiveLinkAnimationFrameData& AnimationData = *FrameData.Cast<FLiveLinkAnimationFrameData>();

			AnimationData.Transforms.Add(UnrealTransform);
			AnimationData.WorldTime = StreamTime;
			LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
		}
		else if (StreamMode == FLightStreamMode::Light)
		{

			FLiveLinkFrameDataStruct FrameData(FLiveLinkLightFrameData::StaticStruct());
			FLiveLinkLightFrameData& TransformData = *FrameData.Cast<FLiveLinkLightFrameData>();

			TransformData.Transform = UnrealTransform;
			TransformData.WorldTime = StreamTime;

			MFnLight Light(RootDagPath);
			TransformData.Intensity = Light.intensity();
			TransformData.LightColor = MayaColorToUnreal(Light.color());

			if (RootDagPath.hasFn(MFn::kSpotLight))
			{
				MFnSpotLight SpotLight(RootDagPath);
				TransformData.InnerConeAngle = static_cast<float>(SpotLight.coneAngle());
				TransformData.OuterConeAngle = static_cast<float>(SpotLight.penumbraAngle());
			}
			LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
		}
	}

	virtual void SetStreamType(const FString& StreamTypeIn) override
	{
		for (int32 StreamTypeIdx = 0; StreamTypeIdx < LightStreamOptions.Num(); ++StreamTypeIdx)
		{
			if (LightStreamOptions[StreamTypeIdx] == StreamTypeIn && StreamMode != (FLightStreamMode)StreamTypeIdx)
			{
				StreamMode = (FLightStreamMode)StreamTypeIdx;
				RebuildSubjectData();
				return;
			}
		}
	}

private:
	FName SubjectName;
	MDagPath RootDagPath;

	const TArray<FString> LightStreamOptions = { TEXT("Root Only"), TEXT("Full Hierarchy"), TEXT("Light") };
	enum FLightStreamMode
	{
		RootOnly,
		FullHierarchy,
		Light,
	};
	FLightStreamMode StreamMode;
};

struct FLiveLinkStreamedPropSubject : IStreamedEntity
{
public:
	FLiveLinkStreamedPropSubject(FName InSubjectName, MDagPath InRootPath)
		: SubjectName(InSubjectName)
		, RootDagPath(InRootPath)
		, StreamMode(FPropStreamMode::RootOnly)
	{}

	~FLiveLinkStreamedPropSubject()
	{
		if (LiveLinkProvider.IsValid())
		{
			LiveLinkProvider->RemoveSubject(SubjectName);
		}
	}

	virtual bool ShouldDisplayInUI() const override { return true; }
	virtual MDagPath GetDagPath() const override { return RootDagPath; }
	virtual MString GetNameDisplayText() const override { return MString(*SubjectName.ToString()); }
	virtual MString GetRoleDisplayText() const override { return MString(*(PropStreamOptions[StreamMode])); }
	virtual MString GetSubjectTypeDisplayText() const override { return MString("Prop"); }

	virtual bool ValidateSubject() const override {return true;}

	virtual void RebuildSubjectData() override
	{
		if (StreamMode == FPropStreamMode::RootOnly)
		{
			FLiveLinkStaticDataStruct TransformData(FLiveLinkTransformStaticData::StaticStruct());
			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkTransformRole::StaticClass(), MoveTemp(TransformData));
		}
		else if (StreamMode == FPropStreamMode::FullHierarchy)
		{
			FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
			FLiveLinkSkeletonStaticData& AnimationData = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

			AnimationData.BoneNames.Add(FName("root"));
			AnimationData.BoneParents.Add(-1);

			LiveLinkProvider->UpdateSubjectStaticData(SubjectName, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
		}
	}

	virtual void OnStream(double StreamTime, int32 FrameNumber) override
	{
		MFnTransform TransformNode(RootDagPath);
		MMatrix MayaTransform = TransformNode.transformation().asMatrix();
		RotateCoordinateSystemForUnreal(MayaTransform);
		const FTransform UnrealTransform = BuildUETransformFromMayaTransform(MayaTransform);

		if (StreamMode == FPropStreamMode::RootOnly)
		{
			FLiveLinkFrameDataStruct FrameData(FLiveLinkTransformFrameData::StaticStruct());
			FLiveLinkTransformFrameData& TransformData = *FrameData.Cast<FLiveLinkTransformFrameData>();

			TransformData.Transform = UnrealTransform;
			TransformData.WorldTime = StreamTime;
			LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
		}
		else if (StreamMode == FPropStreamMode::FullHierarchy)
		{
			FLiveLinkFrameDataStruct FrameData(FLiveLinkAnimationFrameData::StaticStruct());
			FLiveLinkAnimationFrameData& AnimationData = *FrameData.Cast<FLiveLinkAnimationFrameData>();

			AnimationData.Transforms.Add(UnrealTransform);
			AnimationData.WorldTime = StreamTime;
			LiveLinkProvider->UpdateSubjectFrameData(SubjectName, MoveTemp(FrameData));
		}
	}

	virtual void SetStreamType(const FString& StreamTypeIn) override
	{
		for (int32 StreamTypeIdx = 0; StreamTypeIdx < PropStreamOptions.Num(); ++StreamTypeIdx)
		{
			if (PropStreamOptions[StreamTypeIdx] == StreamTypeIn && StreamMode != (FPropStreamMode)StreamTypeIdx)
			{
				StreamMode = (FPropStreamMode)StreamTypeIdx;
				RebuildSubjectData();
				return;
			}
		}
	}

private:
	FName SubjectName;
	MDagPath RootDagPath;

	const TArray<FString> PropStreamOptions = { TEXT("Root Only"), TEXT("Full Hierarchy") };
	enum FPropStreamMode
	{
		RootOnly,
		FullHierarchy,
	};
	FPropStreamMode StreamMode;
};

class FLiveLinkStreamedSubjectManager
{
private:
	TArray<TSharedPtr<IStreamedEntity>> Subjects;

	void ValidateSubjects()
	{
		Subjects.RemoveAll([](const TSharedPtr<IStreamedEntity>& Item)
		{
			return !Item->ValidateSubject();
		});
		RefreshUI();
	}

public:

	FLiveLinkStreamedSubjectManager()
	{
		Reset();
	}

	void GetSubjectNames(TArray<MString>& Entries) const
	{
		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			if (Subject->ShouldDisplayInUI())
			{
				Entries.Add(Subject->GetNameDisplayText());
			}
		}
	}

	void GetSubjectPaths(TArray<MString>& Entries) const
	{
		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			if (Subject->ShouldDisplayInUI())
			{
				Entries.Add(Subject->GetDagPath().fullPathName());
			}
		}
	}

	void GetSubjectRoles(TArray<MString>& Entries) const
	{
		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			if (Subject->ShouldDisplayInUI())
			{
				Entries.Add(Subject->GetRoleDisplayText());
			}
		}
	}

	void GetSubjectTypes(TArray<MString>& Entries) const
	{
		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			if (Subject->ShouldDisplayInUI())
			{
				Entries.Add(Subject->GetSubjectTypeDisplayText());
			}
		}
	}

	template<class SubjectType, typename... ArgsType>
	TSharedPtr<SubjectType> AddSubjectOfType(ArgsType&&... Args)
	{
		TSharedPtr<SubjectType> Subject = MakeShareable(new SubjectType(Args...));

		Subject->RebuildSubjectData();

		int32 FrameNumber = MAnimControl::currentTime().value();
		Subject->OnStream(FPlatformTime::Seconds(), FrameNumber);

		Subjects.Add(Subject);
		return Subject;
	}

	void AddJointHeirarchySubject(FName SubjectName, MDagPath RootPath)
	{
		AddSubjectOfType<FLiveLinkStreamedJointHierarchySubject>(SubjectName, RootPath);
	}

	void AddCameraSubject(FName SubjectName, MDagPath RootPath)
	{
		AddSubjectOfType<FLiveLinkStreamedCameraSubject>(SubjectName, RootPath);
	}

	void AddLightSubject(FName SubjectName, MDagPath RootPath)
	{
		AddSubjectOfType<FLiveLinkStreamedLightSubject>(SubjectName, RootPath);
	}

	void AddPropSubject(FName SubjectName, MDagPath RootPath)
	{
		AddSubjectOfType<FLiveLinkStreamedPropSubject>(SubjectName, RootPath);
	}

	void RemoveSubject(MString PathOfSubjectToRemove)
	{
		for (int32 Index = Subjects.Num() - 1; Index >= 0; --Index)
		{
			if (Subjects[Index]->ShouldDisplayInUI())
			{
				if (Subjects[Index]->GetDagPath().fullPathName() == PathOfSubjectToRemove)
				{
					Subjects.RemoveAt(Index);
					break;
				}
			}
		}
	}

	void ChangeSubjectName(MString SubjectDagPath, MString NewName)
	{
		if (IStreamedEntity* Subject = GetSubjectByDagPath(SubjectDagPath))
		{
			const MDagPath PathBackup = Subject->GetDagPath(); // store so we can re-create the subject
			RemoveSubject(SubjectDagPath);

			if (PathBackup.hasFn(MFn::kJoint))
			{
				AddJointHeirarchySubject(NewName.asChar(), PathBackup);
			}
			else if (PathBackup.hasFn(MFn::kCamera))
			{
				AddCameraSubject(NewName.asChar(), PathBackup);
			}
			else if (PathBackup.hasFn(MFn::kLight))
			{
				AddLightSubject(NewName.asChar(), PathBackup);
			}
			else
			{
				AddPropSubject(NewName.asChar(), PathBackup);
			}
		}
	}

	void ChangeStreamType(MString SubjectPathIn, MString StreamTypeIn)
	{
		if (IStreamedEntity* Subject = GetSubjectByDagPath(SubjectPathIn))
		{
			const FString StreamType(StreamTypeIn.asChar());
			Subject->SetStreamType(StreamType);
		}
	}

	IStreamedEntity* GetSubjectByDagPath(MString Path)
	{
		TSharedPtr<IStreamedEntity>* Found = Subjects.FindByPredicate([&Path](TSharedPtr<IStreamedEntity>& Subject)
		{
			if (Subject.IsValid())
			{
				if (Subject->GetDagPath().fullPathName() == Path)
				{
					return Subject;
				}
			}
			return TSharedPtr<IStreamedEntity>();
		});

		return (*Found).Get();
	}

	void Reset()
	{
		Subjects.Reset();
		AddSubjectOfType<FLiveLinkStreamedActiveCamera>();
	}

	void RebuildSubjects()
	{
		ValidateSubjects();
		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			Subject->RebuildSubjectData();
		}
	}

	void StreamSubjects() const
	{
		double StreamTime = FPlatformTime::Seconds();
		int32 FrameNumber = MAnimControl::currentTime().value();

		for (const TSharedPtr<IStreamedEntity>& Subject : Subjects)
		{
			Subject->OnStream(StreamTime, FrameNumber);
		}
	}
};

const MString LiveLinkSubjectNamesCommandName("LiveLinkSubjectNames");

class LiveLinkSubjectNamesCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkSubjectNamesCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		TArray<MString> SubjectNames;
		LiveLinkStreamManager->GetSubjectNames(SubjectNames);

		for (const MString& Entry : SubjectNames)
		{
			appendToResult(Entry);
		}

		return MS::kSuccess;
	}
};

const MString LiveLinkSubjectPathsCommandName("LiveLinkSubjectPaths");

class LiveLinkSubjectPathsCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkSubjectPathsCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		TArray<MString> SubjectPaths;
		LiveLinkStreamManager->GetSubjectPaths(SubjectPaths);

		for (const MString& Entry : SubjectPaths)
		{
			appendToResult(Entry);
		}

		return MS::kSuccess;
	}
};

const MString LiveLinkSubjectRolesCommandName("LiveLinkSubjectRoles");

class LiveLinkSubjectRolesCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkSubjectRolesCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		TArray<MString> SubjectRoles;
		LiveLinkStreamManager->GetSubjectRoles(SubjectRoles);

		for (const MString& Entry : SubjectRoles)
		{
			appendToResult(Entry);
		}

		return MS::kSuccess;
	}
};

const MString LiveLinkSubjectTypesCommandName("LiveLinkSubjectTypes");

class LiveLinkSubjectTypesCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkSubjectTypesCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		TArray<MString> SubjectTypes;
		LiveLinkStreamManager->GetSubjectTypes(SubjectTypes);

		for (const MString& Entry : SubjectTypes)
		{
			appendToResult(Entry);
		}

		return MS::kSuccess;
	}
};

const MString LiveLinkAddSelectionCommandName("LiveLinkAddSelection");

class LiveLinkAddSelectionCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkAddSelectionCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		MSelectionList SelectedItems;
		MGlobal::getActiveSelectionList(SelectedItems);

		for (unsigned int i = 0; i < SelectedItems.length(); ++i)
		{
			MObject SelectedRoot;
			SelectedItems.getDependNode(i, SelectedRoot);

			bool ItemAdded = false;

			MItDag DagIterator;
			DagIterator.reset(SelectedRoot);
			// first try to find a specific subject item under the selected root item.
			// we iterate through the DAG to find items in groups/sets and to be able to find the Shape compoments which
			// hold the interesting properties.
			while (!DagIterator.isDone() && !ItemAdded)
			{
				MDagPath CurrentItemPath;
				if (!DagIterator.getPath(CurrentItemPath))
					continue;

				MFnDagNode CurrentNode(CurrentItemPath);
				const char* SubjectName = CurrentNode.name().asChar();

				if (CurrentItemPath.hasFn(MFn::kJoint))
				{
					LiveLinkStreamManager->AddJointHeirarchySubject(SubjectName, CurrentItemPath);
					ItemAdded = true;
				}
				else if (CurrentItemPath.hasFn(MFn::kCamera))
				{
					LiveLinkStreamManager->AddCameraSubject(SubjectName, CurrentItemPath);
					ItemAdded = true;
				}
				else if (CurrentItemPath.hasFn(MFn::kLight))
				{
					LiveLinkStreamManager->AddLightSubject(SubjectName, CurrentItemPath);
					ItemAdded = true;
				}

				if (ItemAdded)
				{
					MGlobal::displayInfo(MString("LiveLinkAddSubjectCommand ") + SubjectName);
				}

				DagIterator.next();
			}

			// if there was no specific item, we assume that the selected item is a prop.
			// the props are handled differently because almost everything has a kTransform function set, so if a subject
			// is under a group node or in a set, the group would be added as a prop otherwise.
			if (!ItemAdded)
			{
				if (SelectedRoot.hasFn(MFn::kTransform))
				{
					MFnDagNode DagNode(SelectedRoot);

					MDagPath SubjectPath;
					DagNode.getPath(SubjectPath);

					LiveLinkStreamManager->AddPropSubject(DagNode.name().asChar(), SubjectPath);
				}
			}
		}
		return MS::kSuccess;
	}
};

const MString LiveLinkRemoveSubjectCommandName("LiveLinkRemoveSubject");

class LiveLinkRemoveSubjectCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkRemoveSubjectCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		MSyntax Syntax;
		Syntax.addArg(MSyntax::kString);

		MArgDatabase argData(Syntax, args);

		MString SubjectToRemove;
		argData.getCommandArgument(0, SubjectToRemove);

		LiveLinkStreamManager->RemoveSubject(SubjectToRemove);

		return MS::kSuccess;
	}
};

const MString LiveLinkChangeSubjectNameCommandName("LiveLinkChangeSubjectName");

class LiveLinkChangeSubjectNameCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkChangeSubjectNameCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		MSyntax Syntax;
		Syntax.addArg(MSyntax::kString);
		Syntax.addArg(MSyntax::kString);

		MArgDatabase argData(Syntax, args);

		MString SubjectDagPath;
		MString NewName;
		argData.getCommandArgument(0, SubjectDagPath);
		argData.getCommandArgument(1, NewName);

		LiveLinkStreamManager->ChangeSubjectName(SubjectDagPath, NewName);

		return MS::kSuccess;
	}
};

const MString LiveLinkConnectionStatusCommandName("LiveLinkConnectionStatus");

class LiveLinkConnectionStatusCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkConnectionStatusCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		MString ConnectionStatus("No Provider (internal error)");
		bool bConnection = false;

		if(LiveLinkProvider.IsValid())
		{
			if (LiveLinkProvider->HasConnection())
			{
				ConnectionStatus = "Connected";
				bConnection = true;
			}
			else
			{
				ConnectionStatus = "No Connection";
			}
		}

		appendToResult(ConnectionStatus);
		appendToResult(bConnection);

		return MS::kSuccess;
	}
};

const MString LiveLinkChangeSubjectStreamTypeCommandName("LiveLinkChangeSubjectStreamType");

class LiveLinkChangeSubjectStreamTypeCommand : public MPxCommand
{
public:
	static void		cleanup() {}
	static void*	creator() { return new LiveLinkChangeSubjectStreamTypeCommand(); }

	MStatus			doIt(const MArgList& args) override
	{
		MSyntax Syntax;
		Syntax.addArg(MSyntax::kString);
		Syntax.addArg(MSyntax::kString);

		MArgDatabase argData(Syntax, args);

		MString SubjectPath;
		argData.getCommandArgument(0, SubjectPath);
		MString StreamType;
		argData.getCommandArgument(1, StreamType);

		LiveLinkStreamManager->ChangeStreamType(SubjectPath, StreamType);

		return MS::kSuccess;
	}
};

void OnForceChange(MTime& time, void* clientData)
{
	LiveLinkStreamManager->StreamSubjects();
}

class FMayaOutputDevice : public FOutputDevice
{
public:
	FMayaOutputDevice() : bAllowLogVerbosity(false) {}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		if ((bAllowLogVerbosity && Verbosity <= ELogVerbosity::Log) || (Verbosity <= ELogVerbosity::Display))
		{
			MGlobal::displayInfo(V);
		}
	}

private:

	bool bAllowLogVerbosity;

};

void OnScenePreOpen(void* client)
{
	LiveLinkStreamManager->Reset();
	RefreshUI();
}

void OnSceneOpen(void* client)
{
	//BuildStreamHierarchyData();
}

void AllDagChangesCallback(
	MDagMessage::DagMessage msgType,
	MDagPath &child,
	MDagPath &parent,
	void *clientData)
{
	LiveLinkStreamManager->RebuildSubjects();
}

void OnConnectionStatusChanged()
{
	MGlobal::executeCommand("MayaLiveLinkRefreshConnectionUI");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TMap<uintptr_t, MCallbackId> PostRenderCallbackIds;
TMap<uintptr_t, MCallbackId> ViewportDeletedCallbackIds;

void OnPostRenderViewport(const MString &str, void* ClientData)
{
	LiveLinkStreamManager->StreamSubjects();
}

void OnViewportClosed(void* ClientData)
{
	uintptr_t ViewIndex = reinterpret_cast<uintptr_t>(ClientData);

	MMessage::removeCallback(PostRenderCallbackIds[ViewIndex]);
	PostRenderCallbackIds.Remove(ViewIndex);

	MMessage::removeCallback(ViewportDeletedCallbackIds[ViewIndex]);
	ViewportDeletedCallbackIds.Remove(ViewIndex);
}

void ClearViewportCallbacks()
{
	for (TPair<uintptr_t, MCallbackId>& Pair : PostRenderCallbackIds)
	{
		MMessage::removeCallback(Pair.Value);
	}
	PostRenderCallbackIds.Reset();

	for (TPair<uintptr_t, MCallbackId>& Pair : ViewportDeletedCallbackIds)
	{
		MMessage::removeCallback(Pair.Value);
	}
	ViewportDeletedCallbackIds.Reset();
}

MStatus RefreshViewportCallbacks()
{
	MStatus ExitStatus;

	if (int(M3dView::numberOf3dViews()) != PostRenderCallbackIds.Num())
	{
		ClearViewportCallbacks();

		static MString ListEditorPanelsCmd = "gpuCacheListModelEditorPanels";
		
		MStringArray EditorPanels;
		ExitStatus = MGlobal::executeCommand(ListEditorPanelsCmd, EditorPanels);
		MCHECKERROR(ExitStatus, "gpuCacheListModelEditorPanels");

		if (ExitStatus == MStatus::kSuccess)
		{
			for (uintptr_t i = 0; i < EditorPanels.length(); ++i)
			{
				MStatus Status;
				MCallbackId CallbackId = MUiMessage::add3dViewPostRenderMsgCallback(EditorPanels[i], OnPostRenderViewport, NULL, &Status);

				MREPORTERROR(Status, "MUiMessage::add3dViewPostRenderMsgCallback()");

				if (Status != MStatus::kSuccess)
				{
					ExitStatus = MStatus::kFailure;
					continue;
				}

				PostRenderCallbackIds.Add(i, CallbackId);

				CallbackId = MUiMessage::addUiDeletedCallback(EditorPanels[i], OnViewportClosed, reinterpret_cast<void*>(i), &Status);
				
				MREPORTERROR(Status, "MUiMessage::addUiDeletedCallback()");
				
				if (Status != MStatus::kSuccess)
				{
					ExitStatus = MStatus::kFailure;
					continue;
				}
				ViewportDeletedCallbackIds.Add(i, CallbackId);
			}
		}
	}

	return ExitStatus;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void OnInterval(float elapsedTime, float lastTime, void* clientData)
{
	//No good way to check for new views being created, so just periodically refresh our list
	RefreshViewportCallbacks();

	OnConnectionStatusChanged();

	FTicker::GetCoreTicker().Tick(elapsedTime);
}

/**
* This function is called by Maya when the plugin becomes loaded
*
* @param	MayaPluginObject	The Maya object that represents our plugin
*
* @return	MS::kSuccess if everything went OK and the plugin is ready to use
*/
DLLEXPORT MStatus initializePlugin(MObject MayaPluginObject)
{
	if(!bUEInitialized)
	{
		GEngineLoop.PreInit(TEXT("MayaLiveLinkPlugin -Messaging"));
		ProcessNewlyLoadedUObjects();

		// ensure target platform manager is referenced early as it must be created on the main thread
		GetTargetPlatformManager();

		// Tell the module manager is may now process newly-loaded UObjects when new C++ modules are loaded
		FModuleManager::Get().StartProcessingNewlyLoadedObjects();

		FModuleManager::Get().LoadModule(TEXT("UdpMessaging"));

		GLog->TearDown(); //clean up existing output devices
		GLog->AddOutputDevice(new FMayaOutputDevice()); //Add Maya output device

		LiveLinkProvider = ILiveLinkProvider::CreateLiveLinkProvider(TEXT("Maya Live Link"));
		ConnectionStatusChangedHandle = LiveLinkProvider->RegisterConnStatusChangedHandle(FLiveLinkProviderConnectionStatusChanged::FDelegate::CreateStatic(&OnConnectionStatusChanged));

		bUEInitialized = true; // Dont redo this part if someone unloads and reloads our plugin
	}

	// Tell Maya about our plugin
	MFnPlugin MayaPlugin(
		MayaPluginObject,
		"MayaLiveLinkPlugin",
		"v1.0");

	// We do not tick the core engine but we need to tick the ticker to make sure the message bus endpoint in LiveLinkProvider is
	// up to date
	FTicker::GetCoreTicker().Tick(1.f);

	LiveLinkStreamManager = MakeShareable(new FLiveLinkStreamedSubjectManager());


	MCallbackId forceUpdateCallbackId = MDGMessage::addForceUpdateCallback((MMessage::MTimeFunction)OnForceChange);
	myCallbackIds.append(forceUpdateCallbackId);

	MCallbackId ScenePreOpenedCallbackID = MSceneMessage::addCallback(MSceneMessage::kBeforeOpen, (MMessage::MBasicFunction)OnScenePreOpen);
	myCallbackIds.append(ScenePreOpenedCallbackID);

	MCallbackId SceneOpenedCallbackId = MSceneMessage::addCallback(MSceneMessage::kAfterOpen, (MMessage::MBasicFunction)OnSceneOpen);
	myCallbackIds.append(SceneOpenedCallbackId);

	MCallbackId dagChangedCallbackId = MDagMessage::addAllDagChangesCallback(AllDagChangesCallback);
	myCallbackIds.append(dagChangedCallbackId);

	// Update function every 5 seconds
	MCallbackId timerCallback = MTimerMessage::addTimerCallback(5.f, (MMessage::MElapsedTimeFunction)OnInterval);
	myCallbackIds.append(timerCallback);

	MayaPlugin.registerCommand(LiveLinkSubjectNamesCommandName, LiveLinkSubjectNamesCommand::creator);
	MayaPlugin.registerCommand(LiveLinkSubjectPathsCommandName, LiveLinkSubjectPathsCommand::creator);
	MayaPlugin.registerCommand(LiveLinkSubjectRolesCommandName, LiveLinkSubjectRolesCommand::creator);
	MayaPlugin.registerCommand(LiveLinkSubjectTypesCommandName, LiveLinkSubjectTypesCommand::creator);
	MayaPlugin.registerCommand(LiveLinkAddSelectionCommandName, LiveLinkAddSelectionCommand::creator);
	MayaPlugin.registerCommand(LiveLinkRemoveSubjectCommandName, LiveLinkRemoveSubjectCommand::creator);
	MayaPlugin.registerCommand(LiveLinkChangeSubjectNameCommandName, LiveLinkChangeSubjectNameCommand::creator);
	MayaPlugin.registerCommand(LiveLinkConnectionStatusCommandName, LiveLinkConnectionStatusCommand::creator);
	MayaPlugin.registerCommand(LiveLinkChangeSubjectStreamTypeCommandName, LiveLinkChangeSubjectStreamTypeCommand::creator);

	// Print to Maya's output window, too!
	UE_LOG(LogBlankMayaPlugin, Display, TEXT("MayaLiveLinkPlugin initialized"));

	RefreshViewportCallbacks();

	const MStatus MayaStatusResult = MS::kSuccess;
	return MayaStatusResult;
}


/**
* Called by Maya either at shutdown, or when the user opts to unload the plugin through the Plugin Manager
*
* @param	MayaPluginObject	The Maya object that represents our plugin
*
* @return	MS::kSuccess if everything went OK and the plugin was fully shut down
*/
DLLEXPORT MStatus uninitializePlugin(MObject MayaPluginObject)
{
	// Make sure the Garbage Collector does not try to remove Delete Listeners on shutdown as those will be invalid causing a crash
	GIsRequestingExit = true;

	// Get the plugin API for the plugin object
	MFnPlugin MayaPlugin(MayaPluginObject);

	// ... do stuff here ...

	MayaPlugin.deregisterCommand(LiveLinkSubjectNamesCommandName);
	MayaPlugin.deregisterCommand(LiveLinkSubjectPathsCommandName);
	MayaPlugin.deregisterCommand(LiveLinkSubjectRolesCommandName);
	MayaPlugin.deregisterCommand(LiveLinkSubjectTypesCommandName);
	MayaPlugin.deregisterCommand(LiveLinkAddSelectionCommandName);
	MayaPlugin.deregisterCommand(LiveLinkRemoveSubjectCommandName);
	MayaPlugin.deregisterCommand(LiveLinkChangeSubjectNameCommandName);
	MayaPlugin.deregisterCommand(LiveLinkConnectionStatusCommandName);
	MayaPlugin.deregisterCommand(LiveLinkChangeSubjectStreamTypeCommandName);

	ClearViewportCallbacks();
	if (myCallbackIds.length() != 0)
	{
		// Make sure we remove all the callbacks we added
		MMessage::removeCallbacks(myCallbackIds);
	}

	if (ConnectionStatusChangedHandle.IsValid())
	{
		LiveLinkProvider->UnregisterConnStatusChangedHandle(ConnectionStatusChangedHandle);
		ConnectionStatusChangedHandle.Reset();
	}

	// Maya 2016 does not clean up the address space when unloading a plugin.
	// So if we cleaned up here it would crash in the init above when trying to load the plugin a second.
	const MString MayaApiVersion = MayaPlugin.apiVersion();
	if (FString(MayaApiVersion.asChar()).Compare("201700") >= 0)
	{
		LiveLinkProvider.Reset();
		LiveLinkStreamManager.Reset();

		GEngineLoop.AppPreExit();
		FModuleManager::Get().UnloadModulesAtShutdown();
		GEngineLoop.AppExit();
	}

	MGlobal::executeCommand("MayaLiveLinkClearUI");

	FTicker::GetCoreTicker().Tick(1.f);

	const MStatus MayaStatusResult = MS::kSuccess;
	return MayaStatusResult;
}
