/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * =============================================================================
 * SourceMod
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "ShareSys.h"
#include "ExtensionSys.h"
#include <ILibrarySys.h>
#include "common_logic.h"
#include "PluginSys.h"
#include "HandleSys.h"
#include <assert.h>

using namespace ke;

ShareSystem g_ShareSys;
static unsigned int g_mark_serial = 0;

ShareSystem::ShareSystem()
{
	m_IdentRoot.ident = 0;
	m_TypeRoot = 0;
	m_IfaceType = 0;
	m_CoreType = 0;
}

IdentityToken_t *ShareSystem::CreateCoreIdentity()
{
	if (!m_CoreType)
	{
		m_CoreType = CreateIdentType("CORE");
	}

	return CreateIdentity(m_CoreType, this);
}

void ShareSystem::Initialize()
{
	TypeAccess sec;

	handlesys->InitAccessDefaults(&sec, NULL);
	sec.ident = GetIdentRoot();

	m_TypeRoot = handlesys->CreateType("Identity", this, 0, &sec, NULL, NULL, NULL);
	m_IfaceType = handlesys->CreateType("Interface", this, 0, NULL, NULL, GetIdentRoot(), NULL);

	/* Initialize our static identity handle */
	m_IdentRoot.ident = handlesys->CreateHandle(m_TypeRoot, NULL, NULL, GetIdentRoot(), NULL);

	/* Add the Handle System and others... they are too innocent and pure to do it themselves */
	AddInterface(NULL, handlesys);
	AddInterface(NULL, libsys);
}

void ShareSystem::OnSourceModShutdown()
{
	if (m_CoreType)
	{
		handlesys->RemoveType(m_CoreType, GetIdentRoot());
	}

	handlesys->RemoveType(m_IfaceType, GetIdentRoot());
	handlesys->RemoveType(m_TypeRoot, GetIdentRoot());
}

IdentityType_t ShareSystem::FindIdentType(const char *name)
{
	HandleType_t type;

	if (handlesys->FindHandleType(name, &type))
	{
		if (g_HandleSys.TypeCheck(type, m_TypeRoot))
		{
			return type;
		}
	}

	return 0;
}

IdentityType_t ShareSystem::CreateIdentType(const char *name)
{
	if (!m_TypeRoot)
	{
		return 0;
	}

	return handlesys->CreateType(name, this, m_TypeRoot, NULL, NULL, GetIdentRoot(), NULL);
}

void ShareSystem::OnHandleDestroy(HandleType_t type, void *object)
{
	/* THIS WILL NEVER BE CALLED FOR ANYTHING WITH THE IDENTITY TYPE */
}

IdentityToken_t *ShareSystem::CreateIdentity(IdentityType_t type, void *ptr)
{
	if (!m_TypeRoot)
	{
		return 0;
	}

	/* :TODO: Cache? */
	IdentityToken_t *pToken = new IdentityToken_t;

	HandleSecurity sec;
	sec.pOwner = sec.pIdentity = GetIdentRoot();

	pToken->ident = g_HandleSys.CreateHandleInt(type, NULL, &sec, NULL, NULL, true);
	pToken->ptr = ptr;
	pToken->type = type;

	return pToken;
}

bool ShareSystem::AddInterface(IExtension *myself, SMInterface *iface)
{
	if (!iface)
	{
		return false;
	}

	IfaceInfo info;

	info.owner = myself;
	info.iface = iface;
	
	m_Interfaces.push_back(info);

	return true;
}

bool ShareSystem::RequestInterface(const char *iface_name, 
								   unsigned int iface_vers, 
								   IExtension *myself, 
								   SMInterface **pIface)
{
	/* See if the interface exists */
	List<IfaceInfo>::iterator iter;
	SMInterface *iface;
	IExtension *iface_owner;
	bool found = false;
	for (iter=m_Interfaces.begin(); iter!=m_Interfaces.end(); iter++)
	{
		IfaceInfo &info = (*iter);
		iface = info.iface;
		if (strcmp(iface->GetInterfaceName(), iface_name) == 0)
		{
			if (iface->GetInterfaceVersion() == iface_vers
				|| iface->IsVersionCompatible(iface_vers))
			{
				iface_owner = info.owner;
				found = true;
				break;
			}
		}
	}

	if (!found)
	{
		return false;
	}

	/* Add a dependency node */
	if (iface_owner)
	{
		IfaceInfo info;
		info.iface = iface;
		info.owner = iface_owner;
		g_Extensions.BindDependency(myself, &info);
	}

	if (pIface)
	{
		*pIface = iface;
	}

	return true;
}

void ShareSystem::AddNatives(IExtension *myself, const sp_nativeinfo_t *natives)
{
	CNativeOwner *pOwner;

	pOwner = g_Extensions.GetNativeOwner(myself);

	pOwner->AddNatives(natives);
}

void ShareSystem::DestroyIdentity(IdentityToken_t *identity)
{
	HandleSecurity sec;

	sec.pOwner = GetIdentRoot();
	sec.pIdentity = GetIdentRoot();

	handlesys->FreeHandle(identity->ident, &sec);
	delete identity;
}

void ShareSystem::DestroyIdentType(IdentityType_t type)
{
	handlesys->RemoveType(type, GetIdentRoot());
}

void ShareSystem::RemoveInterfaces(IExtension *pExtension)
{
	List<IfaceInfo>::iterator iter = m_Interfaces.begin();

	while (iter != m_Interfaces.end())
	{
		if ((*iter).owner == pExtension)
		{
			iter = m_Interfaces.erase(iter);
		}
		else
		{
			iter++;
		}
	}
}

void ShareSystem::AddDependency(IExtension *myself, const char *filename, bool require, bool autoload)
{
	g_Extensions.AddDependency(myself, filename, require, autoload);
}

void ShareSystem::RegisterLibrary(IExtension *myself, const char *name)
{
	g_Extensions.AddLibrary(myself, name);
}

void ShareSystem::OverrideNatives(IExtension *myself, const sp_nativeinfo_t *natives)
{
	assert(false);
}

PassRef<Native> ShareSystem::FindNative(const char *name)
{
	NativeCache::Result r = m_NtvCache.find(name);
	if (!r.found())
		return NULL;
	return *r;
}

void ShareSystem::BindNativesToPlugin(CPlugin *pPlugin, bool bCoreOnly)
{
	sp_native_t *native;
	uint32_t i, native_count;
	IPluginContext *pContext;

	pContext = pPlugin->GetBaseContext();

	/* Generate a new serial ID, mark our dependencies with it. */
	g_mark_serial++;
	pPlugin->PropagateMarkSerial(g_mark_serial);

	native_count = pContext->GetNativesNum();
	for (i = 0; i < native_count; i++)
	{
		if (pContext->GetNativeByIndex(i, &native) != SP_ERROR_NONE)
			continue;

		// If we're already bound, no need to do anything else.
		if (native->status == SP_NATIVE_BOUND)
			continue;

		/* Otherwise, the native must be in our cache. */
		Ref<Native> pEntry = FindNative(native->name);
		if (!pEntry)
			continue;

		if (bCoreOnly && pEntry->owner != &g_CoreNatives)
			continue;

		BindNativeToPlugin(pPlugin, native, i, pEntry);
	}
}

void ShareSystem::BindNativeToPlugin(CPlugin *pPlugin, const Ref<Native> &entry)
{
	if (!entry->owner)
		return;

	IPluginContext *pContext = pPlugin->GetBaseContext();

	uint32_t i;
	if (pContext->FindNativeByName(entry->name(), &i) != SP_ERROR_NONE)
		return;

	sp_native_t *native;
	if (pContext->GetNativeByIndex(i, &native) != SP_ERROR_NONE)
		return;

	if (native->status == SP_NATIVE_BOUND)
		return;

	BindNativeToPlugin(pPlugin, native, i, entry);
}

void ShareSystem::BindNativeToPlugin(CPlugin *pPlugin, sp_native_t *native, uint32_t index,
                                     const Ref<Native> &pEntry)
{
	/* Mark as bound... we do the rest next. */
	native->status = SP_NATIVE_BOUND;
	native->pfn = pEntry->func();

	if (pEntry->fake)
	{
		/* This native is not necessarily optional, but we don't guarantee
		 * that its address is long-lived. */
		native->flags |= SP_NTVFLAG_EPHEMERAL;
	}

	/* We don't bother with dependency crap if the owner is Core. */
	if (pEntry->owner != &g_CoreNatives)
	{
		/* The native is optional, this is a special case */
		if ((native->flags & SP_NTVFLAG_OPTIONAL) == SP_NTVFLAG_OPTIONAL)
		{
			/* Only add if there is a valid owner. */
			if (pEntry->owner)
				pEntry->owner->AddWeakRef(WeakNative(pPlugin, index));
			else
				native->status = SP_NATIVE_UNBOUND;
		}
		/* Otherwise, we're a strong dependent and not a weak one */
		else
		{
			/* See if this has already been marked as a dependent.
			 * If it has, it means this relationship has already occurred, 
			 * and there is no reason to do it again.
			 */
			if (pEntry->owner != pPlugin->ToNativeOwner() 
				&& pEntry->owner->GetMarkSerial() != g_mark_serial)
			{
				/* This has not been marked as a dependency yet */
				//pPlugin->AddDependency(pEntry->owner);
				pEntry->owner->AddDependent(pPlugin);
				pEntry->owner->SetMarkSerial(g_mark_serial);
			}
		}
	}
}

PassRef<Native> ShareSystem::AddNativeToCache(CNativeOwner *pOwner, const sp_nativeinfo_t *ntv)
{
	NativeCache::Insert i = m_NtvCache.findForAdd(ntv->name);
	if (i.found())
		return NULL;

	Ref<Native> entry = Newborn<Native>(new Native(pOwner, ntv));
	m_NtvCache.insert(ntv->name, entry);
	return entry;
}

FakeNative::~FakeNative()
{
	g_pSourcePawn2->DestroyFakeNative(gate);
}

void ShareSystem::ClearNativeFromCache(CNativeOwner *pOwner, const char *name)
{
	NativeCache::Result r = m_NtvCache.find(name);
	if (!r.found())
		return;

	Ref<Native> entry(*r);
	if (entry->owner != pOwner)
		return;

	// Clear out the owner bit as a sanity measure.
	entry->owner = NULL;

	m_NtvCache.remove(r);
}

PassRef<Native> ShareSystem::AddFakeNative(IPluginFunction *pFunc, const char *name, SPVM_FAKENATIVE_FUNC func)
{
	Ref<Native> entry(FindNative(name));
	if (entry)
		return NULL;

	AutoPtr<FakeNative> fake(new FakeNative(name, pFunc));

	fake->gate = g_pSourcePawn2->CreateFakeNative(func, fake);
	if (!fake->gate)
		return NULL;

	CNativeOwner *owner = g_PluginSys.GetPluginByCtx(fake->ctx->GetContext());

	entry = Newborn<Native>(new Native(owner, fake.take()));
	m_NtvCache.insert(name, entry);

	return entry;
}

void ShareSystem::AddCapabilityProvider(IExtension *myself, IFeatureProvider *provider,
		                                const char *name)
{
	if (m_caps.contains(name))
		return;

	Capability cap;
	cap.ext = myself;
	cap.provider = provider;

	m_caps.insert(name, cap);
}

void ShareSystem::DropCapabilityProvider(IExtension *myself, IFeatureProvider *provider,
		                                 const char *name)
{
	StringHashMap<Capability>::Result r = m_caps.find(name);
	if (!r.found())
		return;
	if (r->value.ext != myself || r->value.provider != provider)
		return;

	m_caps.remove(r);
}

FeatureStatus ShareSystem::TestFeature(IPluginRuntime *pRuntime, FeatureType feature, 
                                       const char *name)
{
	switch (feature)
	{
	case FeatureType_Native:
		return TestNative(pRuntime, name);
	case FeatureType_Capability:
		return TestCap(name);
	default:
		break;
	}

	return FeatureStatus_Unknown;
}

FeatureStatus ShareSystem::TestNative(IPluginRuntime *pRuntime, const char *name)
{
	uint32_t index;

	if (pRuntime->FindNativeByName(name, &index) == SP_ERROR_NONE)
	{
		sp_native_t *native;
		if (pRuntime->GetNativeByIndex(index, &native) == SP_ERROR_NONE)
		{
			if (native->status == SP_NATIVE_BOUND)
				return FeatureStatus_Available;
			else
				return FeatureStatus_Unknown;
		}
	}

	Ref<Native> entry = FindNative(name);
	if (!entry)
		return FeatureStatus_Unknown;

	return FeatureStatus_Unavailable;
}

FeatureStatus ShareSystem::TestCap(const char *name)
{
	StringHashMap<Capability>::Result r = m_caps.find(name);
	if (!r.found())
		return FeatureStatus_Unknown;

	return r->value.provider->GetFeatureStatus(FeatureType_Capability, name);
}
