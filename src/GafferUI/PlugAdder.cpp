//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2016, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "boost/bind.hpp"

#include "IECoreGL/Texture.h"
#include "IECoreGL/Selector.h"

#include "Gaffer/UndoContext.h"
#include "Gaffer/ScriptNode.h"
#include "Gaffer/Switch.h"
#include "Gaffer/ArrayPlug.h"
#include "Gaffer/Metadata.h"

#include "GafferUI/Nodule.h"
#include "GafferUI/ImageGadget.h"
#include "GafferUI/PlugAdder.h"
#include "GafferUI/Style.h"
#include "GafferUI/ConnectionGadget.h"

using namespace Imath;
using namespace IECore;
using namespace Gaffer;
using namespace GafferUI;

//////////////////////////////////////////////////////////////////////////
// Internal utilities
//////////////////////////////////////////////////////////////////////////

namespace
{

static IECoreGL::Texture *texture( Style::State state )
{
	static IECoreGL::TexturePtr normalTexture = NULL;
	static IECoreGL::TexturePtr highlightedTexture = NULL;

	IECoreGL::TexturePtr &texture = state == Style::HighlightedState ? highlightedTexture : normalTexture;
	if( !texture )
	{
		texture = ImageGadget::textureLoader()->load(
			state == Style::HighlightedState ? "plugAdderHighlighted.png" : "plugAdder.png"
		);

		IECoreGL::Texture::ScopedBinding binding( *texture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	}
	return texture.get();
}

V3f edgeTangent( StandardNodeGadget::Edge edge )
{
	switch( edge )
	{
		case StandardNodeGadget::TopEdge :
			return V3f( 0, 1, 0 );
		case StandardNodeGadget::BottomEdge :
			return V3f( 0, -1, 0 );
		case StandardNodeGadget::LeftEdge :
			return V3f( -1, 0, 0 );
		default :
			return V3f( 1, 0, 0 );
	}
}

StandardNodeGadget::Edge oppositeEdge( StandardNodeGadget::Edge edge )
{
	switch( edge )
	{
		case StandardNodeGadget::TopEdge :
			return StandardNodeGadget::BottomEdge;
		case StandardNodeGadget::BottomEdge :
			return StandardNodeGadget::TopEdge;
		case StandardNodeGadget::LeftEdge :
			return StandardNodeGadget::RightEdge;
		default :
			return StandardNodeGadget::LeftEdge;
	}
}

const char *edgeName( StandardNodeGadget::Edge edge )
{
	switch( edge )
	{
		case StandardNodeGadget::TopEdge :
			return "top";
		case StandardNodeGadget::BottomEdge :
			return "bottom";
		case StandardNodeGadget::LeftEdge :
			return "left";
		default :
			return "right";
	}
}

const char *edgeOrientation( StandardNodeGadget::Edge edge )
{
	switch( edge )
	{
		case StandardNodeGadget::TopEdge :
		case StandardNodeGadget::BottomEdge :
			return "x";
		default :
			return "y";
	}
}

void updateMetadata( Plug *plug, InternedString key, const char *value )
{
	ConstStringDataPtr s = Metadata::value<StringData>( plug, key );
	if( s && s->readable() == value )
	{
		// Metadata already has the value we want. No point adding on
		// an instance override with the exact same value.
		return;
	}

	Metadata::registerValue( plug, key, new StringData( value ) );
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// PlugAdder
//////////////////////////////////////////////////////////////////////////

IE_CORE_DEFINERUNTIMETYPED( PlugAdder );

PlugAdder::PlugAdder( Gaffer::NodePtr node, StandardNodeGadget::Edge edge )
	:	m_node( node ), m_edge( edge ), m_dragging( false )
{
	node->childAddedSignal().connect( boost::bind( &PlugAdder::childAdded, this ) );
	node->childRemovedSignal().connect( boost::bind( &PlugAdder::childRemoved, this ) );

	enterSignal().connect( boost::bind( &PlugAdder::enter, this, ::_1, ::_2 ) );
	leaveSignal().connect( boost::bind( &PlugAdder::leave, this, ::_1, ::_2 ) );
	buttonPressSignal().connect( boost::bind( &PlugAdder::buttonPress, this, ::_1,  ::_2 ) );
	dragBeginSignal().connect( boost::bind( &PlugAdder::dragBegin, this, ::_1, ::_2 ) );
	dragEnterSignal().connect( boost::bind( &PlugAdder::dragEnter, this, ::_2 ) );
	dragMoveSignal().connect( boost::bind( &PlugAdder::dragMove, this, ::_1, ::_2 ) );
	dragLeaveSignal().connect( boost::bind( &PlugAdder::dragLeave, this, ::_2 ) );
	dropSignal().connect( boost::bind( &PlugAdder::drop, this, ::_2 ) );
	dragEndSignal().connect( boost::bind( &PlugAdder::dragEnd, this, ::_2 ) );

	updateVisibility();
}

PlugAdder::~PlugAdder()
{
}

Imath::Box3f PlugAdder::bound() const
{
	return Box3f( V3f( -0.5f, -0.5f, 0.0f ), V3f( 0.5f, 0.5f, 0.0f ) );
}

void PlugAdder::updateDragEndPoint( const Imath::V3f position, const Imath::V3f &tangent )
{
	m_dragPosition = position;
	m_dragTangent = tangent;
	m_dragging = true;
	requestRender();
}

void PlugAdder::doRender( const Style *style ) const
{
	if( m_dragging )
	{
		if( !IECoreGL::Selector::currentSelector() )
		{
			V3f srcTangent( 0.0f, 0.0f, 0.0f );
			style->renderConnection( V3f( 0 ), srcTangent, m_dragPosition, m_dragTangent, Style::HighlightedState );
		}
	}

	float radius = 0.75f;
	Style::State state = Style::NormalState;
	if( getHighlighted() )
	{
		radius = 1.25f;
		state = Style::HighlightedState;
	}
	style->renderImage( Box2f( V2f( -radius ), V2f( radius ) ), texture( state ) );
}

void PlugAdder::addPlug( Gaffer::Plug *connectionEndPoint )
{
	UndoContext undoContext( m_node->ancestor<ScriptNode>() );

	if( SwitchComputeNode *switchNode = runTimeCast<SwitchComputeNode>( m_node.get() ) )
	{
		switchNode->setup( connectionEndPoint );
		ArrayPlug *inPlug = switchNode->getChild<ArrayPlug>( "in" );
		Plug *outPlug = switchNode->getChild<Plug>( "out" );

		StandardNodeGadget::Edge inEdge = StandardNodeGadget::InvalidEdge;
		if( connectionEndPoint->direction() == Plug::Out )
		{
			inPlug->getChild<Plug>( 0 )->setInput( connectionEndPoint );
			inEdge = m_edge;
		}
		else
		{
			connectionEndPoint->setInput( switchNode->getChild<Plug>( "out" ) );
			inEdge = oppositeEdge( m_edge );
		}

		updateMetadata( inPlug, "nodeGadget:nodulePosition", edgeName( inEdge ) );
		updateMetadata( inPlug, "compoundNodule:orientation", edgeOrientation( inEdge ) );
		updateMetadata( outPlug, "nodeGadget:nodulePosition", edgeName( oppositeEdge( inEdge ) ) );
	}
}

void PlugAdder::childAdded()
{
	updateVisibility();
}

void PlugAdder::childRemoved()
{
	updateVisibility();
}

void PlugAdder::updateVisibility()
{
	if( SwitchComputeNode *switchNode = runTimeCast<SwitchComputeNode>( m_node.get() ) )
	{
		setVisible( switchNode->getChild<ArrayPlug>( "in" ) == NULL );
	}
}

void PlugAdder::enter( GadgetPtr gadget, const ButtonEvent &event )
{
	setHighlighted( true );
}

void PlugAdder::leave( GadgetPtr gadget, const ButtonEvent &event )
{
	setHighlighted( false );
}

bool PlugAdder::buttonPress( GadgetPtr gadget, const ButtonEvent &event )
{
	return event.buttons == ButtonEvent::Left;
}

IECore::RunTimeTypedPtr PlugAdder::dragBegin( GadgetPtr gadget, const ButtonEvent &event )
{
	return this;
}

bool PlugAdder::dragEnter( const DragDropEvent &event )
{
	if( event.buttons != DragDropEvent::Left )
	{
		return false;
	}

	if( event.sourceGadget == this )
	{
		updateDragEndPoint( event.line.p0, V3f( 0 ) );
		return true;
	}

	const Plug *plug = runTimeCast<Plug>( event.data.get() );
	if( !plug )
	{
		return false;
	}

	setHighlighted( true );

	V3f center = V3f( 0.0f ) * fullTransform();
	center = center * event.sourceGadget->fullTransform().inverse();
	const V3f tangent = edgeTangent( m_edge );

	if( Nodule *sourceNodule = runTimeCast<Nodule>( event.sourceGadget.get() ) )
	{
		sourceNodule->updateDragEndPoint( center, tangent );
	}
	else if( ConnectionGadget *connectionGadget = runTimeCast<ConnectionGadget>( event.sourceGadget.get() ) )
	{
		connectionGadget->updateDragEndPoint( center, tangent );
	}

	return true;
}

bool PlugAdder::dragMove( GadgetPtr gadget, const DragDropEvent &event )
{
	m_dragPosition = event.line.p0;
	requestRender();
	return true;
}

bool PlugAdder::dragLeave( const DragDropEvent &event )
{
	setHighlighted( false );
	return true;
}

bool PlugAdder::drop( const DragDropEvent &event )
{
	setHighlighted( false );

	if( Plug *plug = runTimeCast<Plug>( event.data.get() ) )
	{
		addPlug( plug );
		return true;
	}

	return false;
}

bool PlugAdder::dragEnd( const DragDropEvent &event )
{
	m_dragging = false;
	requestRender();
	return false;
}