import sys
import inspect

import maya
import maya.OpenMaya as OpenMaya
import maya.OpenMayaMPx as OpenMayaMPx
import maya.cmds as cmds
from pymel.core.windows import Callback, CallbackWithArgs

StreamTypesPerSubjectType = {
								"Prop": 		["Root Only", "Full Hierarchy"],
								"Character":	["Root Only", "Full Hierarchy"],
								"Camera":		["Root Only", "Full Hierarchy", "Camera"],
								"Light":		["Root Only", "Full Hierarchy", "Light"],
							}

def OnRemoveSubject(SubjectPath):
	cmds.LiveLinkRemoveSubject(SubjectPath)
	RefreshSubjects()

def CreateSubjectTable():
	cmds.rowColumnLayout("SubjectLayout", numberOfColumns=5, columnWidth=[(1, 20), (2,80), (3, 100), (4, 180), (5, 120)], columnOffset=[(1, 'right', 5), (2, 'right', 10), (4, 'left', 10)], parent="SubjectWrapperLayout")
	cmds.text(label="")
	cmds.text(label="Subject Type", font="boldLabelFont", align="left")
	cmds.text(label="Subject Name", font="boldLabelFont", align="left")
	cmds.text(label="DAG Path", font="boldLabelFont", align="left")
	cmds.text(label="Stream Type", font="boldLabelFont", align="left")
	cmds.rowColumnLayout("SubjectLayout", edit=True, rowOffset=(1, "bottom", 10))

#Populate subjects list from c++
def PopulateSubjects():
	SubjectNames = cmds.LiveLinkSubjectNames()
	SubjectPaths = cmds.LiveLinkSubjectPaths()
	SubjectTypes = cmds.LiveLinkSubjectTypes()
	SubjectRoles = cmds.LiveLinkSubjectRoles()
	if SubjectPaths is not None:
		RowCounter = 0
		for (SubjectName, SubjectPath, SubjectType, SubjectRole) in zip(SubjectNames, SubjectPaths, SubjectTypes, SubjectRoles):
			cmds.button(label="-", height=21, command=Callback(OnRemoveSubject, SubjectPath), parent="SubjectLayout")
			cmds.text(label=SubjectType, height=21, align="left", parent="SubjectLayout")
			cmds.textField(text=SubjectName, height=21, changeCommand=CallbackWithArgs(cmds.LiveLinkChangeSubjectName, SubjectPath), parent="SubjectLayout")
			cmds.text(label=SubjectPath, align="left", height=21, parent="SubjectLayout")

			LayoutName = "ColumnLayoutRow_" + str(RowCounter) # adding a trailing index makes the name unique which is required by the api

			cmds.columnLayout(LayoutName, parent="SubjectLayout")
			cmds.optionMenu("SubjectTypeMenu", parent=LayoutName, height=21, changeCommand=CallbackWithArgs(cmds.LiveLinkChangeSubjectStreamType, SubjectPath))
			
			for StreamType in StreamTypesPerSubjectType[SubjectType]:
				cmds.menuItem(label=StreamType)
			
			StreamTypeIndex = StreamTypesPerSubjectType[SubjectType].index(SubjectRole) + 1 # menu items are 1-indexed
			cmds.optionMenu("SubjectTypeMenu", edit=True, select=StreamTypeIndex)
			
			RowCounter += 1

def ClearSubjects():
	if (cmds.window(MayaLiveLinkUI.WindowName , exists=True)):
		cmds.deleteUI("SubjectLayout")

#Refresh subjects list
def RefreshSubjects():
	if (cmds.window(MayaLiveLinkUI.WindowName , exists=True)):
		cmds.deleteUI("SubjectLayout")
		CreateSubjectTable()
		PopulateSubjects()

#Connection UI Colours
ConnectionActiveColour = [0.71, 0.9, 0.1]
ConnectionInactiveColour = [1.0, 0.4, 0.4]
ConnectionColourMap = {
	True : ConnectionActiveColour,
	False: ConnectionInactiveColour
}

#Base class for command (common creator method + allows for automatic register/unregister)
class LiveLinkCommand(OpenMayaMPx.MPxCommand):
	def __init__(self):
		OpenMayaMPx.MPxCommand.__init__(self)

	@classmethod
	def Creator(Cls):
		return OpenMayaMPx.asMPxPtr( Cls() )

# Is supplied object a live link command
def IsLiveLinkCommand(InCls):
	return inspect.isclass(InCls) and issubclass(InCls, LiveLinkCommand) and InCls != LiveLinkCommand

# Given a list of strings of names return all the live link commands listed
def GetLiveLinkCommandsFromModule(ModuleItems):
	EvalItems = (eval(Item) for Item in ModuleItems)
	return [Command for Command in EvalItems if IsLiveLinkCommand(Command) ]

# Command to create the Live Link UI
class MayaLiveLinkUI(LiveLinkCommand):
	WindowName = "MayaLiveLinkUI"
	Title = "Maya Live Link UI"
	WindowSize = (500, 300)

	def __init__(self):
		LiveLinkCommand.__init__(self)
		
	# Invoked when the command is run.
	def doIt(self,argList):
		if (cmds.window(self.WindowName , exists=True)):
			cmds.deleteUI(self.WindowName)
		window = cmds.window( self.WindowName, title=self.Title, widthHeight=(self.WindowSize[0], self.WindowSize[1]) )
		
		#Get current connection status
		ConnectionText, ConnectedState = cmds.LiveLinkConnectionStatus()
		
		cmds.columnLayout( "mainColumn", adjustableColumn=True )
		cmds.rowLayout("HeaderRow", numberOfColumns=3, adjustableColumn=1, parent = "mainColumn")
		cmds.text(label="Unreal Engine Live Link", align="left")
		cmds.text(label="Connection:")
		cmds.text("ConnectionStatusUI", label=ConnectionText, align="center", backgroundColor=ConnectionColourMap[ConnectedState], width=150)
		
		cmds.separator(h=20, style="none", parent="mainColumn")
		cmds.columnLayout("SubjectWrapperLayout", parent="mainColumn") # just used as a container that will survive refreshing, so the following controls stay in their correct place

		CreateSubjectTable()
		PopulateSubjects()

		cmds.separator(h=20, style="none", parent="mainColumn")
		cmds.button( label='Add Selection', parent = "mainColumn", command=self.AddSelection)

		cmds.showWindow( self.WindowName )

	def AddSelection(self, *args):
		cmds.LiveLinkAddSelection()
		RefreshSubjects()

# Command to Refresh the subject UI
class MayaLiveLinkRefreshUI(LiveLinkCommand):
	def __init__(self):
		LiveLinkCommand.__init__(self)
		
	# Invoked when the command is run.
	def doIt(self,argList):
		RefreshSubjects()

class MayaLiveLinkClearUI(LiveLinkCommand):
	def __init__(self):
		LiveLinkCommand.__init__(self)
	
	def doIt(self, argList):
		ClearSubjects()
		CreateSubjectTable()

# Command to Refresh the connection UI
class MayaLiveLinkRefreshConnectionUI(LiveLinkCommand):
	def __init__(self):
		LiveLinkCommand.__init__(self)
		
	# Invoked when the command is run.
	def doIt(self,argList):
		if (cmds.window(MayaLiveLinkUI.WindowName , exists=True)):
			#Get current connection status
			ConnectionText, ConnectedState = cmds.LiveLinkConnectionStatus()
			cmds.text("ConnectionStatusUI", edit=True, label=ConnectionText, backgroundColor=ConnectionColourMap[ConnectedState])

class MayaLiveLinkGetActiveCamera(LiveLinkCommand):
	def __init__(self):
		LiveLinkCommand.__init__(self)
		
	# Invoked when the command is run.
	def doIt(self,argList):
		self.clearResult()
		try:
			c = cmds.getPanel(wf=1)
			cam = cmds.modelPanel(c, q=True, camera=True)
		except:
			pass
		else:
			self.setResult(cam)

#Grab commands declared
Commands = GetLiveLinkCommandsFromModule(dir())

#Initialize the script plug-in
def initializePlugin(mobject):
	mplugin = OpenMayaMPx.MFnPlugin(mobject)

	print "LiveLink:"
	for Command in Commands:
		try:
			print "\tRegistering Command '%s'"%Command.__name__
			mplugin.registerCommand( Command.__name__, Command.Creator )
		except:
			sys.stderr.write( "Failed to register command: %s\n" % Command.__name__ )
			raise

# Uninitialize the script plug-in
def uninitializePlugin(mobject):
	mplugin = OpenMayaMPx.MFnPlugin(mobject)

	for Command in Commands:
		try:
			mplugin.deregisterCommand( Command.__name__ )
		except:
			sys.stderr.write( "Failed to unregister command: %s\n" % Command.__name__ )