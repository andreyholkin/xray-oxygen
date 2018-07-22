// xrCDB.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#pragma hdrstop
#include "xrCDB.h"

#include "../FrayBuildConfig.hpp"
#include "../../3rd-party/OPCODE/Opcode.h"
#include "../../3rd-party/OPCODE/OPC_TreeBuilders.h"
#include "../../3rd-party/OPCODE/OPC_Model.h"
#include "xrCDB_Model.h"

using namespace CDB;
using namespace Opcode;

bool APIENTRY DllMain(HANDLE hModule, u32  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return true;
}

// Model building
MODEL::MODEL()
{
	tree = 0;
	tris = 0;
	tris_count = 0;
	verts = 0;
	verts_count = 0;
	status = S_INIT;
}
MODEL::~MODEL()
{
	syncronize();		// maybe model still in building
	status = S_INIT;
	xr_delete(tree);
	xr_free(tris);		tris_count = 0;
	xr_free(verts);	verts_count = 0;
}

struct	BTHREAD_params
{
	MODEL*				M;
	Fvector*			V;
	int					Vcnt;
	TRI*				T;
	int					Tcnt;
	void*				pCache;
	bool				isCacheReader;
	build_callback*		BC;
	void*				BCP;
    bool                rebuildTrisRequired;
};

void MODEL::build_thread(void *params)
{
	_initialize_cpu_thread();
	FPU::m64r();
	BTHREAD_params	P = *((BTHREAD_params*)params);
	std::lock_guard<std::recursive_mutex> lock(P.M->cs);
	P.M->build_internal(P.V, P.Vcnt, P.T, P.Tcnt, P.pCache, P.isCacheReader, P.BC, P.BCP, P.rebuildTrisRequired);
	P.M->status = S_READY;
}

void MODEL::build(Fvector* V, int Vcnt, TRI* T, int Tcnt, void* pCache, bool isCacheReader, build_callback* bc, void* bcp, bool rebuildTrisRequired)
{
	R_ASSERT(S_INIT == status);
	R_ASSERT((Vcnt >= 4) && (Tcnt >= 2));
	_initialize_cpu_thread();

	const unsigned cpu_thrd = CPU::Info.n_threads;

	BTHREAD_params P = { this, V, Vcnt, T, Tcnt, pCache, isCacheReader, bc, bcp, rebuildTrisRequired };
#ifdef CD_BUILDER_DEBUG
	build_thread((void*)&P);
#else
	thread_spawn(build_thread, "CDB-construction", 0, &P);
	while (S_INIT == status)
	{
		Sleep(5);
	}
#endif
}

void MODEL::build_internal(Fvector* V, int Vcnt, TRI* T, int Tcnt, void* pCache, bool isCacheReader, build_callback* bc, void* bcp, bool rebuildTrisRequired)
{
    IReader* cacheFileReader = (IReader*)pCache;

    struct AutoCloseCacheFile
    {
        AutoCloseCacheFile(IReader* InCacheFileReader)
            : CacheFileReader(InCacheFileReader)
        {}

        ~AutoCloseCacheFile()
        {
            FS.r_close(CacheFileReader);
        }

        IReader* CacheFileReader;
    } AutoCloser(cacheFileReader);

	// verts
	verts_count = Vcnt;
	verts = CALLOC(Fvector, verts_count);
	std::memcpy(verts, V, verts_count * sizeof(Fvector));

	// tris
	tris_count = Tcnt;
	tris = CALLOC(TRI, tris_count);

    if (rebuildTrisRequired)
    {
        TRI_DEPRECATED* realT = reinterpret_cast<TRI_DEPRECATED*> (T);
        for (int triIter = 0; triIter < tris_count; ++triIter)
        {
            TRI_DEPRECATED& oldTri = realT[triIter];
            TRI& newTri = tris[triIter];
            newTri = oldTri;
        }
    }
    else
    {
        std::memcpy(tris, T, tris_count * sizeof(TRI));
    }

	// callback
	if (bc)
	{
		bc(verts, Vcnt, tris, Tcnt, bcp);
	}

	// Release data pointers
	status = S_BUILD;

    auto TryRestoreFromCacheLambda = [this, cacheFileReader, isCacheReader]() -> bool
    {
        if (cacheFileReader && isCacheReader)
        {
            tree = xr_new<CDB_Model>();
            return tree->Restore(cacheFileReader);
        }

        Msg("* Level collision DB cache missing, rebuilding...");
        return false;
    };

    // Try restore from cache. If successfull - don't rebuild collision DB
    if (TryRestoreFromCacheLambda()) return;

	// Allocate temporary "OPCODE" tris + convert tris to 'pointer' form
	u32* temp_tris = CALLOC(u32, tris_count * 3);
	if (!temp_tris)
	{
		xr_free(verts);
		xr_free(tris);
		return;
	}

	u32* temp_ptr = temp_tris;
	for (int i = 0; i<tris_count; i++)
	{
		*temp_ptr++ = tris[i].verts[0];
		*temp_ptr++ = tris[i].verts[1];
		*temp_ptr++ = tris[i].verts[2];
	}

	// Build a non quantized no-leaf tree
	OPCODECREATE OPCC = OPCODECREATE();
	OPCC.mIMesh = new MeshInterface();

	OPCC.mIMesh->SetNbTriangles(tris_count);
	OPCC.mIMesh->SetNbVertices(verts_count);
	OPCC.mIMesh->SetPointers((IndexedTriangle*)temp_tris, (Point*)verts);

	OPCC.mSettings.mRules = SplittingRules::SPLIT_SPLATTER_POINTS | SplittingRules::SPLIT_GEOM_CENTER;
	OPCC.mQuantized = false;
	OPCC.mNoLeaf = true;

    // Can be not nullptr, since we can fail restoring from cache file
    xr_delete(tree);
	tree = CNEW(CDB_Model)();
	
	if (!tree->Build(OPCC)) 
	{
		xr_free(verts);
		xr_free(tris);
		xr_free(temp_tris);
		return;
	}

	if (tree && pCache && !isCacheReader)
	{
		tree->Store((IWriter*)pCache);
	}

	// Free temporary tris
	xr_free(temp_tris);
	return;
}

size_t MODEL::memory()
{
	if (S_BUILD == status)
	{
		Msg("! xrCDB: model still isn't ready"); 
		return 0u;
	}
	const u32 V = verts_count * sizeof(Fvector);
	const u32 T = tris_count * sizeof(TRI);
	return tree->GetUsedBytes() + V + T + sizeof(*this) + sizeof(*tree);
}

// This is the constructor of a class that has been exported.
// see xrCDB.h for the class definition
COLLIDER::COLLIDER()
{
	ray_mode = 0;
	box_mode = 0;
	frustum_mode = 0;
}

COLLIDER::~COLLIDER()
{
	r_free();
}

RESULT& COLLIDER::r_add()
{
	rd.push_back(RESULT());
	return rd.back();
}

void COLLIDER::r_free()
{
	rd.clear();
}
