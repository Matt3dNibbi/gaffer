##########################################################################
#  
#  Copyright (c) 2014, Image Engine Design Inc. All rights reserved.
#  
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#  
#      * Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#  
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials provided with
#        the distribution.
#  
#      * Neither the name of John Haddon nor the names of
#        any other contributors to this software may be used to endorse or
#        promote products derived from this software without specific prior
#        written permission.
#  
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#  
##########################################################################

import IECore

import Gaffer
import GafferUI

import GafferScene

class SceneReaderPathPreview( GafferUI.PathPreviewWidget ) :

	def __init__( self, path ) :
			
		column = GafferUI.SplitContainer( GafferUI.SplitContainer.Orientation.Vertical )
		
		GafferUI.PathPreviewWidget.__init__( self, column, path )

		self.__script = Gaffer.ScriptNode( "scenePreview" )
		
		# for reading IECore.SceneInterface files (scc, lscc)
		self.__script["SceneReader"] = GafferScene.SceneReader()
		
		# for reading Alembic files (abc)
		self.__script["AlembicSource"] = GafferScene.AlembicSource()
		
		# for reading more generic single object files (cob, ptc, pdc, etc)
		## \todo: can we unify all file input to SceneReader by creating a SceneInterface that makes
		# single object scenes using Reader ops behind the scenes?
		self.__script["ObjectReader"] = Gaffer.ObjectReader()
		self.__script["ObjectToScene"] = GafferScene.ObjectToScene( "ObjectToScene" )
		self.__script["ObjectToScene"]["object"].setInput( self.__script["ObjectReader"]["out"] )
		
		# display points and curves GL style rather than disks and ribbons
		self.__script["OpenGLAttributes"] = GafferScene.OpenGLAttributes( "OpenGLAttributes" )
		self.__script["OpenGLAttributes"]["attributes"]["pointsPrimitiveUseGLPoints"]["value"].setValue( 'forAll' )
		self.__script["OpenGLAttributes"]["attributes"]["pointsPrimitiveUseGLPoints"]["enabled"].setValue( True )
		self.__script["OpenGLAttributes"]["attributes"]["curvesPrimitiveUseGLLines"]["enabled"].setValue( True )
		
		self.__script["camera"] = _Camera()
		self.__script["camera"]["in"].setInput( self.__script["OpenGLAttributes"]["out"] )
		
		column.append( GafferUI.Viewer( self.__script ) )
		column.append( GafferUI.Timeline( self.__script ) )
		
		self.__script.selection().add( self.__script["camera"] )

		self._updateFromPath()
	
	def isValid( self ) :

		if not isinstance( self.getPath(), Gaffer.FileSystemPath ) or not self.getPath().isLeaf() :
			return False
		
		ext = str( self.getPath() ).split( "." )[-1]
		
		supported = set( [ "abc" ] )
		supported.update( GafferScene.SceneReader.supportedExtensions() )
		supported.update( IECore.Reader.supportedExtensions() )
		# no reason to preview a single image as a 3D scene
		supported.difference_update( IECore.Reader.supportedExtensions( IECore.TypeId.ImageReader ) )
		
		return ext in supported
	
	def _updateFromPath( self ) :
		
		self.__script["SceneReader"]["fileName"].setValue( "" )
		self.__script["AlembicSource"]["fileName"].setValue( "" )
		self.__script["ObjectReader"]["fileName"].setValue( "" )
		
		if not self.isValid() :
			return
		
		fileName = str( self.getPath() )
		ext = fileName.split( "." )[-1]
		
		outPlug = None
		
		if ext in GafferScene.SceneReader.supportedExtensions() :
			
			self.__script["SceneReader"]["fileName"].setValue( fileName )
			outPlug = self.__script["SceneReader"]["out"]
						
			scene = IECore.SharedSceneInterfaces.get( fileName )
			if hasattr( scene, "numBoundSamples" ) :
				numSamples = scene.numBoundSamples()
				if numSamples > 1 :
					startFrame = int( round( scene.boundSampleTime( 0 ) * 24.0 ) )
					endFrame = int( round( scene.boundSampleTime( numSamples - 1 ) * 24.0 ) )
					self.__script["frameRange"]["start"].setValue( startFrame )
					self.__script["frameRange"]["end"].setValue( endFrame )
					GafferUI.Playback.acquire( self.__script.context() ).setFrameRange( startFrame, endFrame )
		
		elif ext in IECore.Reader.supportedExtensions() :
			
			self.__script["ObjectReader"]["fileName"].setValue( fileName )
			outPlug = self.__script["ObjectToScene"]["out"]
		
		elif ext == "abc" :
			
			self.__script["AlembicSource"]["fileName"].setValue( fileName )
			outPlug = self.__script["AlembicSource"]["out"]
		
		self.__script["OpenGLAttributes"]["in"].setInput( outPlug )

GafferUI.PathPreviewWidget.registerType( "Scene", SceneReaderPathPreview )

class _Camera( Gaffer.Node ) :

	def __init__( self, name = "_Camera" ) :
	
		Gaffer.Node.__init__( self, name )
	
		self["in"] = GafferScene.ScenePlug()
		self["addCamera"] = Gaffer.BoolPlug( defaultValue = False )
		self["lookAt"] = Gaffer.StringPlug( defaultValue = "/" )
		self["depth"] = Gaffer.FloatPlug( defaultValue = 20, minValue = 0 )
		self["angle"] = Gaffer.FloatPlug()
		self["elevation"] = Gaffer.FloatPlug( defaultValue = 10, minValue = -90, maxValue = 90 )
		
		self["camera"] = GafferScene.Camera()
		self["camera"]["name"].setValue( "previewCamera" )
		
		self["parent"] = GafferScene.Parent()
		self["parent"]["in"].setInput( self["in"] )
		self["parent"]["parent"].setValue( "/" )
		self["parent"]["child"].setInput( self["camera"]["out"] )
		
		self["cameraFilter"] = GafferScene.PathFilter()
		self["cameraFilter"]["paths"].setValue( IECore.StringVectorData( [ "/previewCamera" ] ) )
		
		self["parentConstraint"] = GafferScene.ParentConstraint()
		self["parentConstraint"]["in"].setInput( self["parent"]["out"] )
		self["parentConstraint"]["target"].setInput( self["lookAt"] )
		self["parentConstraint"]["targetMode"].setValue( self["parentConstraint"].TargetMode.BoundCenter )
		self["parentConstraint"]["filter"].setInput( self["cameraFilter"]["match"] )
		
		self["cameraRotate"] = GafferScene.Transform()
		self["cameraRotate"]["in"].setInput( self["parentConstraint"]["out"] )
		self["cameraRotate"]["transform"]["rotate"]["y"].setInput( self["angle"] )
		self["cameraRotate"]["space"].setValue( self["cameraRotate"].Space.Object )
		self["cameraRotate"]["filter"].setInput( self["cameraFilter"]["match"] )

		self["elevationExpression"] = Gaffer.Expression()
		self["elevationExpression"]["engine"].setValue( "python" )
		self["elevationExpression"]["expression"].setValue( 'parent["cameraRotate"]["transform"]["rotate"]["x"] = -parent["elevation"]' )

		self["cameraTranslate"] = GafferScene.Transform()
		self["cameraTranslate"]["in"].setInput( self["cameraRotate"]["out"] )
		self["cameraTranslate"]["transform"]["translate"]["z"].setInput( self["depth"] )
		self["cameraTranslate"]["space"].setValue( self["cameraRotate"].Space.Object )
		self["cameraTranslate"]["filter"].setInput( self["cameraFilter"]["match"] )
		
		self["options"] = GafferScene.StandardOptions()
		self["options"]["options"]["renderCamera"]["enabled"].setValue( True )
		self["options"]["options"]["renderCamera"]["value"].setValue( "/previewCamera" )
		self["options"]["in"].setInput( self["cameraTranslate"]["out"] )
		
		self["switch"] = GafferScene.SceneSwitch()
		self["switch"]["in"].setInput( self["in"] )
		self["switch"]["in1"].setInput( self["options"]["out"] )
		self["switch"]["index"].setInput( self["addCamera"] )
		
		self["out"] = GafferScene.ScenePlug( direction = Gaffer.Plug.Direction.Out )
		self["out"].setInput( self["switch"]["out"] )
			
IECore.registerRunTimeTyped( _Camera )

GafferUI.NodeToolbar.registerCreator( _Camera.staticTypeId(), GafferUI.StandardNodeToolbar )
GafferUI.PlugValueWidget.registerCreator( _Camera.staticTypeId(), "in", None )
GafferUI.PlugValueWidget.registerCreator( _Camera.staticTypeId(), "out", None )
GafferUI.PlugValueWidget.registerCreator( _Camera.staticTypeId(), "user", None )

GafferUI.PlugValueWidget.registerCreator(
	_Camera.staticTypeId(),
	"lookAt",
	lambda plug : GafferUI.PathPlugValueWidget(
		plug,
		path = GafferScene.ScenePath( plug.node()["in"], plug.node().scriptNode().context(), "/" ),
	),
)

def __fixedWidthNumericPlugValueWidget( plug ) :

	result = GafferUI.NumericPlugValueWidget( plug )
	result.numericWidget().setFixedCharacterWidth( 5 )
	
	return result

GafferUI.PlugValueWidget.registerCreator( _Camera.staticTypeId(), "depth", __fixedWidthNumericPlugValueWidget )
GafferUI.PlugValueWidget.registerCreator( _Camera.staticTypeId(), "angle", __fixedWidthNumericPlugValueWidget )
GafferUI.PlugValueWidget.registerCreator( _Camera.staticTypeId(), "elevation", __fixedWidthNumericPlugValueWidget )
