////////////////////////////////////////////////////////////////////////////
//	Created		: 29.07.2009
//	Author		: Armen Abroyan
//	Copyright (C) GSC Game World - 2009
////////////////////////////////////////////////////////////////////////////

#ifndef STATS_MANAGER_H_INCLUDED
#define STATS_MANAGER_H_INCLUDED

enum enum_stats_buffer_type	{ enum_stats_buffer_type_vertex, enum_stats_buffer_type_index, enum_stats_buffer_type_rtarget, enum_stats_buffer_type_COUNT };

class stats_manager 
{

public:
	~stats_manager ();
 	void increment_stats		( u32 size, enum_stats_buffer_type type, _D3DPOOL location );
	void decrement_stats		( u32 size, enum_stats_buffer_type type, _D3DPOOL location );

	void increment_stats_rtarget( ID3DTexture2D*	buff );
	void increment_stats_vb		( ID3DVertexBuffer*		buff );
	void increment_stats_ib		( ID3DIndexBuffer*		buff );

	void decrement_stats_rtarget( ID3DTexture2D*	buff );
	void decrement_stats_vb		( ID3DVertexBuffer*		buff );
	void decrement_stats_ib		( ID3DIndexBuffer*		buff );
#ifdef USE_DX12
	//#TODO: #GIPERION, ����� �� ������
	// D3D12 ������ ����� ���� ������ ��� ��������� ����������
	void IncrementedRenderTargetStats(D3D12_RESOURCE_DESC* buffer);
#endif
	u32 memory_usage_summary[enum_stats_buffer_type_COUNT][4];

private:

	void increment_stats( u32 size, enum_stats_buffer_type type, _D3DPOOL location, void* buff_ptr );
	void decrement_stats( u32 size, enum_stats_buffer_type type, _D3DPOOL location, void* buff_ptr );

	struct stats_item
	{
		void*					buff_ptr;
		u32						size; 
		enum_stats_buffer_type	type;
		_D3DPOOL				location;
	}; // stats_item

	xr_vector<stats_item>		m_buffers_list;
}; // class stats_manager

u32 get_format_pixel_size ( D3DFORMAT format );

#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_DX12)
u32 get_format_pixel_size ( DXGI_FORMAT format );
#endif

#endif // #ifndef STATS_MANAGER_H_INCLUDED