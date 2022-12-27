#include "Engine.h"
#include "Graphics.h"

namespace Graphics
{
	GraphicsClass::GraphicsClass()
	{
	}

	GraphicsClass::~GraphicsClass()
	{
		if (m_d3dDevice != nullptr)
			FlushCommandQueue();
	}

	void GraphicsClass::Initialize(HWND mainWnd, int width, int height)
	{
		D3DClass::Initialize(mainWnd, width, height);

		// Reset the command list to prep for initialization commands.
		ThrowIfFailed(m_CommandList->Reset(m_DirectCommandListAllocator.Get(), nullptr));

		// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
		// so we have to query this information.
		m_CbvSrvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		m_Camera.SetPosition(0.0f, 8.0f, -40.0f);

		LoadTextures();
		BuildDescriptorHeaps();
		BuildRootSignature();
		BuildShadersAndInputLayout();
		BuildRoomGeometry();
		BuildShapeGeometry();
		BuildCarGeometry();
		BuildMaterials();
		BuildRenderItems();
		BuildFrameResources();
		BuildPipelineStateObjects();

		// Execute the initialization commands.
		ThrowIfFailed(m_CommandList->Close());
		
		ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
		m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		// Wait until initialization is complete.
		FlushCommandQueue();
	}

	void GraphicsClass::OnResize()
	{
		D3DClass::OnResize();
		m_Camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
		BoundingFrustum::CreateFromMatrix(m_CameraFrustum, m_Camera.GetProj());
	}

	void GraphicsClass::Update(const Timer& gameTimer)
	{
		if (m_ImguiManager.AddShapeFlag())
			AddShape();
		if(m_ImguiManager.CreateMaterialFlag())
			AddMaterial();
		
		m_Camera.UpdateCameraPosition(m_ImguiManager.CameraPosition());
		OnKeyboardInput(gameTimer);

		// Cycle through the circular frame resource array.
		m_CurrentFrameResourceIndex = (m_CurrentFrameResourceIndex + 1) % gNumFrameResources;
		m_CurrentFrameResource = m_FrameResources[m_CurrentFrameResourceIndex].get();

		// Has the GPU finished processing the commands of the current frame resource?
		// If not, wait until the GPU has completed commands up to this fence point.
		if (m_CurrentFrameResource->Fence != 0 && m_Fence->GetCompletedValue() < m_CurrentFrameResource->Fence)
		{
			HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
			ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFrameResource->Fence, eventHandle));
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}

		FrustumCulling(gameTimer);
		UpdateShadows(gameTimer);
		UpdateReflections(gameTimer);
		UpdateObjectConstantBuffers(gameTimer);
		UpdateMaterialConstantBuffers(gameTimer);
		UpdateMainPassConstantBuffer(gameTimer);
		UpdateReflectedPassConstantBuffer(gameTimer);
	}

	void GraphicsClass::Draw(const Timer& gameTimer)
	{
		UpdateImGuiData();
		UpdateSceneData();
		m_ImguiManager.NewFrame();
		m_ImguiManager.ShowSetItemsWindow();
		m_ImguiManager.ShowSetCameraWindow();
		m_ImguiManager.ShowSetLightningWindow();
		m_ImguiManager.ShowSetSceneWindow();
		m_ImguiManager.Render();

		auto cmdListAlloc = m_CurrentFrameResource->CmdListAlloc;

		// Reuse the memory associated with command recording.
		// We can only reset when the associated command lists have finished execution on the GPU.
		ThrowIfFailed(cmdListAlloc->Reset());

		// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
		// Reusing the command list reuses memory.
		ThrowIfFailed(m_CommandList->Reset(cmdListAlloc.Get(), m_PipelineStateObjects["opaque"].Get()));

		m_CommandList->RSSetViewports(1, &m_ScreenViewport);
		m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

		// Indicate a state transition on the resource usage.
		D3D12_RESOURCE_BARRIER resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_CommandList->ResourceBarrier(1, &resourceBarrier);

		// Clear the back buffer and depth buffer.
		m_CommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&m_MainPassConstantBuffer.FogColor, 0, nullptr);
		m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		// Specify the buffers we are going to render to.
		D3D12_CPU_DESCRIPTOR_HANDLE currentBackBufferView = CurrentBackBufferView();
		D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = DepthStencilView();
		m_CommandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);

		ID3D12DescriptorHeap* descriptorHeaps[] = { m_ShaderResourceViewDescriptorHeap.Get() };
		m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

		UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

		auto passCB = m_CurrentFrameResource->PassCB->Resource();
		m_CommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

		DrawRenderItems(m_CommandList.Get(), m_RenderItemLayer[(int)RenderLayer::Opaque]);

		// Mark the visible mirror pixels in the stencil buffer with the value 1
		m_CommandList->OMSetStencilRef(1);
		m_CommandList->SetPipelineState(m_PipelineStateObjects["markStencilMirrors"].Get());
		DrawRenderItems(m_CommandList.Get(), m_RenderItemLayer[(int)RenderLayer::Mirrors]);

		// Draw the reflection into the mirror only (only for pixels where the stencil buffer is 1).
		// Note that we must supply a different per-pass constant buffer--one with the lights reflected.
		m_CommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
		m_CommandList->SetPipelineState(m_PipelineStateObjects["drawStencilReflections"].Get());
		DrawRenderItems(m_CommandList.Get(), m_RenderItemLayer[(int)RenderLayer::Reflected]);

		m_CommandList->SetPipelineState(m_PipelineStateObjects["drawShadowReflections"].Get());
		DrawRenderItems(m_CommandList.Get(), m_RenderItemLayer[(int)RenderLayer::ShadowReflected]);

		// Restore main pass constants and stencil ref.
		m_CommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
		m_CommandList->OMSetStencilRef(0);

		// Draw mirror with transparency so reflection blends through.
		m_CommandList->SetPipelineState(m_PipelineStateObjects["transparent"].Get());
		DrawRenderItems(m_CommandList.Get(), m_RenderItemLayer[(int)RenderLayer::Transparent]);

		m_CommandList->SetPipelineState(m_PipelineStateObjects["alphaTested"].Get());
		DrawRenderItems(m_CommandList.Get(), m_RenderItemLayer[(int)RenderLayer::AlphaTested]);

		// Draw shadows
		m_CommandList->SetPipelineState(m_PipelineStateObjects["shadow"].Get());
		DrawRenderItems(m_CommandList.Get(), m_RenderItemLayer[(int)RenderLayer::Shadow]);

		m_CommandList->SetPipelineState(m_PipelineStateObjects["highlight"].Get());
		DrawRenderItems(m_CommandList.Get(), m_RenderItemLayer[(int)RenderLayer::Highlight]);

		m_CommandList->SetDescriptorHeaps(1, &m_ShaderResourceViewHeap);
		m_ImguiManager.DrawRenderData(m_CommandList.Get());

		// Indicate a state transition on the resource usage.
		resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		m_CommandList->ResourceBarrier(1, &resourceBarrier);

		// Done recording commands.
		ThrowIfFailed(m_CommandList->Close());

		// Add the command list to the queue for execution.
		ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
		m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		// Swap the back and front buffers
		ThrowIfFailed(m_SwapChain->Present(0, 0));
		m_CurrentBackBuffer = (m_CurrentBackBuffer + 1) % swapChainBufferCount;

		// Advance the fence value to mark commands up to this fence point.
		m_CurrentFrameResource->Fence = ++m_CurrentFence;

		// Add an instruction to the command queue to set a new fence point. 
		// Because we are on the GPU timeline, the new fence point won't be 
		// set until the GPU finishes processing all the commands prior to this Signal().
		m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence);
	}

	void GraphicsClass::OnRightMouseDown(WPARAM buttonState, int x, int y)
	{
		m_LastMousePos.x = x;
		m_LastMousePos.y = y;
		SetCapture(MainWnd());
		Pick(x, y);
	}

	void GraphicsClass::OnRightMouseUp(WPARAM buttonState, int x, int y)
	{
		ReleaseCapture();
	}

	void GraphicsClass::OnMouseMove(WPARAM buttonState, int x, int y, int z)
	{
		if ((buttonState & MK_CONTROL) != 0)
		{
			// Make each pixel correspond to a quarter of a degree.
			float dx = XMConvertToRadians(0.25f * static_cast<float>(x - m_LastMousePos.x));
			float dy = XMConvertToRadians(0.25f * static_cast<float>(y - m_LastMousePos.y));

			m_Camera.Pitch(dy);
			m_Camera.RotateY(dx);
		}
		else if ((buttonState & MK_RBUTTON) != 0)
		{
			MoveRenderItem(x, y, z);
		}

		m_Camera.UpdateViewMatrix();
		m_LastMousePos.x = x;
		m_LastMousePos.y = y;
	}

	void GraphicsClass::OnMouseWheel(WPARAM buttonState, int x, int y, int z)
	{
		if ((buttonState & MK_CONTROL) == 0 && (buttonState & MK_RBUTTON) == 0)
		{
			if (z < 0)
				m_Camera.Climb(-0.1f);
			if (z > 0)
				m_Camera.Climb(0.1f);
		}

		m_Camera.UpdateViewMatrix();
		m_ImguiManager.CameraPosition(m_Camera.GetPosition3f());

		if ((buttonState & MK_RBUTTON) != 0)
		{
			Pick(x, y);
			MoveRenderItem(x, y, z);
		}

		m_LastMousePos.x = x;
		m_LastMousePos.y = y;
	}

	void GraphicsClass::OnKeyboardInput(const Timer& gameTimer)
	{
		const float dt = gameTimer.DeltaTime();

		if (GetAsyncKeyState('W') & 0x8000)
			m_Camera.Walk(10.0f * dt);

		if (GetAsyncKeyState('S') & 0x8000)
			m_Camera.Walk(-10.0f * dt);

		if (GetAsyncKeyState('A') & 0x8000)
			m_Camera.Strafe(-10.0f * dt);

		if (GetAsyncKeyState('D') & 0x8000)
			m_Camera.Strafe(10.0f * dt);

		m_Camera.UpdateViewMatrix();
		m_ImguiManager.CameraPosition(m_Camera.GetPosition3f());
	}

	void GraphicsClass::FrustumCulling(const Timer& gameTimer)
	{
		XMMATRIX view = m_Camera.GetView();
		XMVECTOR detView = XMMatrixDeterminant(view);
		XMMATRIX invView = XMMatrixInverse(&detView, view);

		for (auto& e : m_AllRenderItems)
		{
			if (e.get()->FrustumTest)
			{
				XMMATRIX world = XMLoadFloat4x4(&e->World);
				XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

				XMVECTOR detWorld = XMMatrixDeterminant(world);
				XMMATRIX invWorld = XMMatrixInverse(&detWorld, world);

				// View space to the object's local space.
				XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);

				// Transform the camera frustum from view space to the object's local space.
				BoundingFrustum localSpaceFrustum;
				m_CameraFrustum.Transform(localSpaceFrustum, viewToLocal);

				// Perform the box/frustum intersection test in local space.
				if ((localSpaceFrustum.Contains(e->Bounds) != DirectX::DISJOINT) || (m_FrustumCullingIsEnabled == false))
					e->FrustumTestResult = true;
				else
					e->FrustumTestResult = false;
			}
		}
	}

	void GraphicsClass::UpdateShadows(const Timer& gameTimer)
	{
		for (auto& e : m_AllRenderItems)
		{
			if (e->ShadowSource != nullptr)
			{
				// Update shadow world matrix.
				XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
				XMVECTOR toMainLight = -XMLoadFloat3(&m_MainPassConstantBuffer.Lights[0].Direction);
				XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
				XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
				XMStoreFloat4x4(&e->World, XMLoadFloat4x4(&e->ShadowSource->World) * S * shadowOffsetY);
				e->NumFramesDirty = gNumFrameResources;
			}
		}
	}

	void GraphicsClass::UpdateReflections(const Timer& gameTimer)
	{
		XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
		XMMATRIX R = XMMatrixReflect(mirrorPlane);
		for (auto& e : m_AllRenderItems)
		{
			if (e->ReflectionSource != nullptr)
			{
				// Update reflection world matrix.
				XMStoreFloat4x4(&e->World, XMLoadFloat4x4(&e->ReflectionSource->World) * R);
				e->NumFramesDirty = gNumFrameResources;
			}
		}
	}

	void GraphicsClass::UpdateObjectConstantBuffers(const Timer& gameTimer)
	{
		auto currObjectCB = m_CurrentFrameResource->ObjectCB.get();
		for (auto& e : m_AllRenderItems)
		{
			// Only update the cbuffer data if the constants have changed.  
			// This needs to be tracked per frame resource.
			if (e->NumFramesDirty > 0)
			{
				DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&e->World);
				DirectX::XMMATRIX texTransform = DirectX::XMLoadFloat4x4(&e->TexTransform);

				ObjectConstants objConstants;
				DirectX::XMStoreFloat4x4(&objConstants.World, DirectX::XMMatrixTranspose(world));
				DirectX::XMStoreFloat4x4(&objConstants.TexTransform, DirectX::XMMatrixTranspose(texTransform));

				currObjectCB->CopyData(e->ObjConstantBufferIndex, objConstants);

				// Next FrameResource need to be updated too.
				e->NumFramesDirty--;
			}
		}
	}

	void GraphicsClass::UpdateMaterialConstantBuffers(const Timer& gameTimer)
	{
		auto currMaterialCB = m_CurrentFrameResource->MaterialCB.get();
		for (auto& e : m_Materials)
		{
			// Only update the cbuffer data if the constants have changed.  If the cbuffer
			// data changes, it needs to be updated for each FrameResource.
			Material* mat = e.second.get();

			if (mat->NumFramesDirty > 0)
			{
				DirectX::XMMATRIX matTransform = DirectX::XMLoadFloat4x4(&mat->MatTransform);

				MaterialConstants matConstants;
				matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
				matConstants.FresnelR0 = mat->FresnelR0;
				matConstants.Roughness = mat->Roughness;
				DirectX::XMStoreFloat4x4(&matConstants.MatTransform, DirectX::XMMatrixTranspose(matTransform));

				currMaterialCB->CopyData(mat->MatCBIndex, matConstants);
				// Next FrameResource need to be updated too.
				mat->NumFramesDirty--;
			}
		}
	}

	void GraphicsClass::UpdateMainPassConstantBuffer(const Timer& gameTimer)
	{
		DirectX::XMMATRIX view = m_Camera.GetView();
		DirectX::XMMATRIX proj = m_Camera.GetProj();

		DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);

		DirectX::XMVECTOR detView = DirectX::XMMatrixDeterminant(view);
		DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&detView, view);

		DirectX::XMVECTOR detProj = DirectX::XMMatrixDeterminant(proj);
		DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(&detProj, proj);

		DirectX::XMVECTOR detViewProj = DirectX::XMMatrixDeterminant(viewProj);
		DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(&detViewProj, viewProj);

		DirectX::XMStoreFloat4x4(&m_MainPassConstantBuffer.View, DirectX::XMMatrixTranspose(view));
		DirectX::XMStoreFloat4x4(&m_MainPassConstantBuffer.InvView, DirectX::XMMatrixTranspose(invView));
		DirectX::XMStoreFloat4x4(&m_MainPassConstantBuffer.Proj, DirectX::XMMatrixTranspose(proj));
		DirectX::XMStoreFloat4x4(&m_MainPassConstantBuffer.InvProj, DirectX::XMMatrixTranspose(invProj));
		DirectX::XMStoreFloat4x4(&m_MainPassConstantBuffer.ViewProj, DirectX::XMMatrixTranspose(viewProj));
		DirectX::XMStoreFloat4x4(&m_MainPassConstantBuffer.InvViewProj, DirectX::XMMatrixTranspose(invViewProj));
		m_MainPassConstantBuffer.EyePosW = m_Camera.GetPosition3f();
		m_MainPassConstantBuffer.RenderTargetSize = DirectX::XMFLOAT2((float)m_ClientWidth, (float)m_ClientHeight);
		m_MainPassConstantBuffer.InvRenderTargetSize = DirectX::XMFLOAT2(1.0f / m_ClientWidth, 1.0f / m_ClientHeight);
		m_MainPassConstantBuffer.NearZ = 1.0f;
		m_MainPassConstantBuffer.FarZ = 1000.0f;
		m_MainPassConstantBuffer.TotalTime = gameTimer.TotalTime();
		m_MainPassConstantBuffer.DeltaTime = gameTimer.DeltaTime();

		UpdateLights();

		auto currPassCB = m_CurrentFrameResource->PassCB.get();
		currPassCB->CopyData(0, m_MainPassConstantBuffer);
	}

	void GraphicsClass::UpdateReflectedPassConstantBuffer(const Timer& gameTimer)
	{
		m_ReflectedPassCB = m_MainPassConstantBuffer;
		
		XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
		XMMATRIX R = XMMatrixReflect(mirrorPlane);

		// Reflect the lighting.
		XMVECTOR lightDir = XMLoadFloat3(&m_MainPassConstantBuffer.Lights[0].Direction);
		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&m_ReflectedPassCB.Lights[0].Direction, reflectedLightDir);
		
		// Reflected pass stored in index 1
		auto currPassCB = m_CurrentFrameResource->PassCB.get();
		currPassCB->CopyData(1, m_ReflectedPassCB);
	}

	void GraphicsClass::LoadTextures()
	{
		auto bricksTex = std::make_unique<Texture>();
		bricksTex->Name = "bricksTex";
		bricksTex->Filename = L"..\\Engine\\Content\\Textures\\bricks3.dds";
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_d3dDevice.Get(),
			m_CommandList.Get(), bricksTex->Filename.c_str(),
			bricksTex->Resource, bricksTex->UploadHeap));

		auto checkboardTex = std::make_unique<Texture>();
		checkboardTex->Name = "checkboardTex";
		checkboardTex->Filename = L"..\\Engine\\Content\\Textures\\checkboard.dds";
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_d3dDevice.Get(),
			m_CommandList.Get(), checkboardTex->Filename.c_str(),
			checkboardTex->Resource, checkboardTex->UploadHeap));

		auto iceTex = std::make_unique<Texture>();
		iceTex->Name = "iceTex";
		iceTex->Filename = L"..\\Engine\\Content\\Textures\\ice.dds";
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_d3dDevice.Get(),
			m_CommandList.Get(), iceTex->Filename.c_str(),
			iceTex->Resource, iceTex->UploadHeap));

		auto white1x1Tex = std::make_unique<Texture>();
		white1x1Tex->Name = "white1x1Tex";
		white1x1Tex->Filename = L"..\\Engine\\Content\\Textures\\white1x1.dds";
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_d3dDevice.Get(),
			m_CommandList.Get(), white1x1Tex->Filename.c_str(),
			white1x1Tex->Resource, white1x1Tex->UploadHeap));

		m_Textures[bricksTex->Name] = std::move(bricksTex);
		m_Textures[checkboardTex->Name] = std::move(checkboardTex);
		m_Textures[iceTex->Name] = std::move(iceTex);
		m_Textures[white1x1Tex->Name] = std::move(white1x1Tex);
	}

	void GraphicsClass::BuildRootSignature()
	{
		CD3DX12_DESCRIPTOR_RANGE texTable;
		texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		// Root parameter can be a table, root descriptor or root constants.
		CD3DX12_ROOT_PARAMETER slotRootParameter[4];

		// Create root CBV.
		slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[1].InitAsConstantBufferView(0);
		slotRootParameter[2].InitAsConstantBufferView(1);
		slotRootParameter[3].InitAsConstantBufferView(2);

		auto staticSamplers = GetStaticSamplers();

		// A root signature is an array of root parameters.
		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
			(UINT)staticSamplers.size(), staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}
		ThrowIfFailed(hr);

		ThrowIfFailed(m_d3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(m_RootSignature.GetAddressOf())));
	}

	void GraphicsClass::BuildDescriptorHeaps()
	{
		// Create the SRV heap.
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 4;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
			&srvHeapDesc, IID_PPV_ARGS(&m_ShaderResourceViewDescriptorHeap)));

		// Fill out the heap with actual descriptors.
		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
			m_ShaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		auto bricksTex = m_Textures["bricksTex"]->Resource;
		auto checkboardTex = m_Textures["checkboardTex"]->Resource;
		auto iceTex = m_Textures["iceTex"]->Resource;
		auto white1x1Tex = m_Textures["white1x1Tex"]->Resource;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = bricksTex->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = -1;
		m_d3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, m_CbvSrvDescriptorSize);

		srvDesc.Format = checkboardTex->GetDesc().Format;
		m_d3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, m_CbvSrvDescriptorSize);

		srvDesc.Format = iceTex->GetDesc().Format;
		m_d3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, m_CbvSrvDescriptorSize);

		srvDesc.Format = white1x1Tex->GetDesc().Format;
		m_d3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);
	}

	void GraphicsClass::BuildShadersAndInputLayout()
	{
		const D3D_SHADER_MACRO defines[] =
		{
			"FOG", "1",
			NULL, NULL
		};

		const D3D_SHADER_MACRO alphaTestDefines[] =
		{
			"FOG", "1",
			"ALPHA_TEST", "1",
			NULL, NULL
		};

		m_Shaders["standardVS"] = d3dUtil::CompileShader(L"..\\Engine\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
		m_Shaders["opaquePS"] = d3dUtil::CompileShader(L"..\\Engine\\Shaders\\Default.hlsl", defines, "PS", "ps_5_0");
		m_Shaders["alphaTestedPS"] = d3dUtil::CompileShader(L"..\\Engine\\Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_0");

		m_InputLayout =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
	}

	void GraphicsClass::BuildRoomGeometry()
	{
		std::array<Vertex, 20> vertices =
		{
			// Floor: Observe we tile texture coordinates.
			Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0 
			Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
			Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
			Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

			// Wall: Observe we tile texture coordinates, and that we
			// leave a gap in the middle for the mirror.
			Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
			Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
			Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
			Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

			Vertex(4.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8 
			Vertex(4.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
			Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
			Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

			Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
			Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
			Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
			Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

			// Mirror
			Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
			Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
			Vertex(4.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
			Vertex(4.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
		};

		std::array<std::int16_t, 30> indices =
		{
			// Floor
			0, 1, 2,
			0, 2, 3,

			// Walls
			4, 5, 6,
			4, 6, 7,

			8, 9, 10,
			8, 10, 11,

			12, 13, 14,
			12, 14, 15,

			// Mirror
			16, 17, 18,
			16, 18, 19
		};

		SubmeshGeometry floorSubmesh;
		floorSubmesh.IndexCount = 6;
		floorSubmesh.StartIndexLocation = 0;
		floorSubmesh.BaseVertexLocation = 0;

		SubmeshGeometry wallSubmesh;
		wallSubmesh.IndexCount = 18;
		wallSubmesh.StartIndexLocation = 6;
		wallSubmesh.BaseVertexLocation = 0;

		SubmeshGeometry mirrorSubmesh;
		mirrorSubmesh.IndexCount = 6;
		mirrorSubmesh.StartIndexLocation = 24;
		mirrorSubmesh.BaseVertexLocation = 0;

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "roomGeo";

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
			m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
			m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R16_UINT;
		geo->IndexBufferByteSize = ibByteSize;

		geo->DrawArgs["floor"] = floorSubmesh;
		geo->DrawArgs["wall"] = wallSubmesh;
		geo->DrawArgs["mirror"] = mirrorSubmesh;

		m_Geometries[geo->Name] = std::move(geo);
	}

	void GraphicsClass::BuildPipelineStateObjects()
	{
		// PSO for opaque objects.
		D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
		ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		opaquePsoDesc.InputLayout = { m_InputLayout.data(), (UINT)m_InputLayout.size() };
		opaquePsoDesc.pRootSignature = m_RootSignature.Get();
		opaquePsoDesc.VS =
		{
			reinterpret_cast<BYTE*>(m_Shaders["standardVS"]->GetBufferPointer()),
			m_Shaders["standardVS"]->GetBufferSize()
		};
		opaquePsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(m_Shaders["opaquePS"]->GetBufferPointer()),
			m_Shaders["opaquePS"]->GetBufferSize()
		};
		opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		opaquePsoDesc.SampleMask = UINT_MAX;
		opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		opaquePsoDesc.NumRenderTargets = 1;
		opaquePsoDesc.RTVFormats[0] = m_BackBufferFormat;
		opaquePsoDesc.SampleDesc.Count = Get4xMsaaState() ? 4 : 1;
		opaquePsoDesc.SampleDesc.Quality = Get4xMsaaState() ? (Get4xMsaaQuality() - 1) : 0;
		opaquePsoDesc.DSVFormat = m_DepthStencilFormat;
		ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["opaque"])));

		// PSO for marking stencil mirrors.
		CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
		mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;
		D3D12_DEPTH_STENCIL_DESC mirrorDSS = { 0 };
		mirrorDSS.DepthEnable = true;
		mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		mirrorDSS.StencilEnable = true;
		mirrorDSS.StencilReadMask = 0xff;
		mirrorDSS.StencilWriteMask = 0xff;
		mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
		mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		// We are not rendering backfacing polygons, so these settings do not matter.
		mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
		mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPsoDesc = opaquePsoDesc;
		markMirrorsPsoDesc.BlendState = mirrorBlendState;
		markMirrorsPsoDesc.DepthStencilState = mirrorDSS;
		ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&markMirrorsPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["markStencilMirrors"])));

		// PSO for stencil reflections.
		D3D12_DEPTH_STENCIL_DESC reflectionsDSS;
		reflectionsDSS.DepthEnable = true;
		reflectionsDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		reflectionsDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		reflectionsDSS.StencilEnable = true;
		reflectionsDSS.StencilReadMask = 0xff;
		reflectionsDSS.StencilWriteMask = 0xff;
		reflectionsDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		reflectionsDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		reflectionsDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		reflectionsDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
		// We are not rendering backfacing polygons, so these settings do not matter.
		reflectionsDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		reflectionsDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		reflectionsDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		reflectionsDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPsoDesc = opaquePsoDesc;
		drawReflectionsPsoDesc.DepthStencilState = reflectionsDSS;
		drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;
		ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&drawReflectionsPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["drawStencilReflections"])));

		// PSO for transparent objects
		D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;
		D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
		transparencyBlendDesc.BlendEnable = true;
		transparencyBlendDesc.LogicOpEnable = false;
		transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
		transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
		transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
		transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
		transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
		ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["transparent"])));

		// PSO for alpha tested objects
		D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
		alphaTestedPsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(m_Shaders["alphaTestedPS"]->GetBufferPointer()),
			m_Shaders["alphaTestedPS"]->GetBufferSize()
		};
		alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["alphaTested"])));

		// PSO for shadow objects
		// We are going to draw shadows with transparency, so base it off the transparency description.
		D3D12_DEPTH_STENCIL_DESC shadowDSS;
		shadowDSS.DepthEnable = true;
		shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		shadowDSS.StencilEnable = true;
		shadowDSS.StencilReadMask = 0xff;
		shadowDSS.StencilWriteMask = 0xff;
		shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
		shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
		// We are not rendering backfacing polygons, so these settings do not matter.
		shadowDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		shadowDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		shadowDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
		shadowDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPsoDesc;
		shadowPsoDesc.DepthStencilState = shadowDSS;
		ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["shadow"])));

		// PSO for shadow reflections
		D3D12_GRAPHICS_PIPELINE_STATE_DESC drawShadowReflectionsPsoDesc = transparentPsoDesc;
		drawShadowReflectionsPsoDesc.DepthStencilState = shadowDSS;
		drawShadowReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		drawShadowReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;
		ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&drawShadowReflectionsPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["drawShadowReflections"])));
		
		// PSO for highlight objects
		D3D12_GRAPHICS_PIPELINE_STATE_DESC highlightPsoDesc = opaquePsoDesc;
		highlightPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		// Change the depth test from < to <= so that if we draw the same triangle twice, it will
		// still pass the depth test.  This is needed because we redraw the picked triangle with a
		// different material to highlight it.  If we do not use <=, the triangle will fail the 
		// depth test the 2nd time we try and draw it.
		highlightPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		// Standard transparency blending.
		transparencyBlendDesc.BlendEnable = true;
		transparencyBlendDesc.LogicOpEnable = false;
		transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
		transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
		transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
		transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
		transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		highlightPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
		ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&highlightPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["highlight"])));
	}

	void GraphicsClass::BuildShapeGeometry()
	{
		GeometryGenerator geoGen;
		GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
		GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
		GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

		//
		// We are concatenating all the geometry into one big vertex/index buffer.  So
		// define the regions in the buffer each submesh covers.
		//

		// Cache the vertex offsets to each object in the concatenated vertex buffer.
		UINT boxVertexOffset = 0;
		UINT sphereVertexOffset = boxVertexOffset + (UINT)box.Vertices.size();
		UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

		// Cache the starting index for each object in the concatenated index buffer.
		UINT boxIndexOffset = 0;
		UINT sphereIndexOffset = boxIndexOffset + (UINT)box.Indices32.size();
		UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

		//
		// Extract the vertex elements we are interested in and pack the
		// vertices of all the meshes into one vertex buffer.
		//
		auto totalVertexCount =
			box.Vertices.size() +
			sphere.Vertices.size() +
			cylinder.Vertices.size();

		std::vector<Vertex> vertices(totalVertexCount);

		XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
		XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

		XMVECTOR vMin = XMLoadFloat3(&vMinf3);
		XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

		UINT k = 0;
		for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = box.Vertices[i].Position;
			vertices[k].Normal = box.Vertices[i].Normal;

			XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

			vertices[i].TexC = { 0.0f, 0.0f };

			vMin = XMVectorMin(vMin, P);
			vMax = XMVectorMax(vMax, P);
		}

		BoundingBox boxBounds;
		XMStoreFloat3(&boxBounds.Center, 0.5f * (vMin + vMax));
		XMStoreFloat3(&boxBounds.Extents, 0.5f * (vMax - vMin));

		vMin = XMLoadFloat3(&vMinf3);
		vMax = XMLoadFloat3(&vMaxf3);

		for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = sphere.Vertices[i].Position;
			vertices[k].Normal = sphere.Vertices[i].Normal;

			XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

			vertices[i].TexC = { 0.0f, 0.0f };

			vMin = XMVectorMin(vMin, P);
			vMax = XMVectorMax(vMax, P);
		}

		BoundingBox sphereBounds;
		XMStoreFloat3(&sphereBounds.Center, 0.5f * (vMin + vMax));
		XMStoreFloat3(&sphereBounds.Extents, 0.5f * (vMax - vMin));

		vMin = XMLoadFloat3(&vMinf3);
		vMax = XMLoadFloat3(&vMaxf3);

		for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = cylinder.Vertices[i].Position;
			vertices[k].Normal = cylinder.Vertices[i].Normal;

			XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

			vertices[i].TexC = { 0.0f, 0.0f };

			vMin = XMVectorMin(vMin, P);
			vMax = XMVectorMax(vMax, P);
		}

		BoundingBox cylinderBounds;
		XMStoreFloat3(&cylinderBounds.Center, 0.5f * (vMin + vMax));
		XMStoreFloat3(&cylinderBounds.Extents, 0.5f * (vMax - vMin));

		std::vector<std::uint16_t> indices;
		indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
		indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
		indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "shapeGeo";

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
			m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
			m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R16_UINT;
		geo->IndexBufferByteSize = ibByteSize;

		SubmeshGeometry boxSubmesh;
		boxSubmesh.IndexCount = (UINT)box.Indices32.size();
		boxSubmesh.StartIndexLocation = boxIndexOffset;
		boxSubmesh.BaseVertexLocation = boxVertexOffset;
		boxSubmesh.Bounds = boxBounds;

		SubmeshGeometry sphereSubmesh;
		sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
		sphereSubmesh.StartIndexLocation = sphereIndexOffset;
		sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
		boxSubmesh.Bounds = sphereBounds;

		SubmeshGeometry cylinderSubmesh;
		cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
		cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
		cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
		boxSubmesh.Bounds = cylinderBounds;

		geo->DrawArgs["box"] = boxSubmesh;
		geo->DrawArgs["sphere"] = sphereSubmesh;
		geo->DrawArgs["cylinder"] = cylinderSubmesh;

		m_Geometries[geo->Name] = std::move(geo);
	}

	void GraphicsClass::BuildCarGeometry()
	{
		std::ifstream fin(L"..\\Engine\\Content\\Models\\car.txt");

		if (!fin)
		{
			MessageBox(0, L"Models/car.txt not found.", 0, 0);
			return;
		}

		UINT vcount = 0;
		UINT tcount = 0;
		std::string ignore;

		fin >> ignore >> vcount;
		fin >> ignore >> tcount;
		fin >> ignore >> ignore >> ignore >> ignore;

		XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
		XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

		XMVECTOR vMin = XMLoadFloat3(&vMinf3);
		XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

		std::vector<Vertex> vertices(vcount);
		for (UINT i = 0; i < vcount; ++i)
		{
			fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
			fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

			XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

			vertices[i].TexC = { 0.0f, 0.0f };

			vMin = XMVectorMin(vMin, P);
			vMax = XMVectorMax(vMax, P);
		}

		BoundingBox bounds;
		XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
		XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

		fin >> ignore;
		fin >> ignore;
		fin >> ignore;

		std::vector<std::int32_t> indices(3 * tcount);
		for (UINT i = 0; i < tcount; ++i)
		{
			fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
		}

		fin.close();

		//
		// Pack the indices of all the meshes into one index buffer.
		//

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "carGeo";

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
			m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
			m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibByteSize;

		SubmeshGeometry submesh;
		submesh.IndexCount = (UINT)indices.size();
		submesh.StartIndexLocation = 0;
		submesh.BaseVertexLocation = 0;
		submesh.Bounds = bounds;

		geo->DrawArgs["car"] = submesh;

		m_Geometries[geo->Name] = std::move(geo);
	}

	void GraphicsClass::BuildFrameResources()
	{
		for (int i = 0; i < gNumFrameResources; ++i)
		{
			m_FrameResources.push_back(std::make_unique<FrameResource>(m_d3dDevice.Get(),
				2, (UINT)m_AllRenderItems.size(), (UINT)m_Materials.size()));
		}
	}

	void GraphicsClass::BuildMaterials()
	{
		auto bricks = std::make_unique<Material>();
		bricks->Name = "bricks";
		bricks->MatCBIndex = m_MatConstantBufferIndex++;
		bricks->DiffuseSrvHeapIndex = 0;
		bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		bricks->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
		bricks->Roughness = 0.25f;

		auto checkertile = std::make_unique<Material>();
		checkertile->Name = "checkertile";
		checkertile->MatCBIndex = m_MatConstantBufferIndex++;
		checkertile->DiffuseSrvHeapIndex = 1;
		checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
		checkertile->Roughness = 0.3f;

		auto icemirror = std::make_unique<Material>();
		icemirror->Name = "icemirror";
		icemirror->MatCBIndex = m_MatConstantBufferIndex++;
		icemirror->DiffuseSrvHeapIndex = 2;
		icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
		icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		icemirror->Roughness = 0.5f;

		auto bone = std::make_unique<Material>();
		bone->Name = "bone";
		bone->MatCBIndex = m_MatConstantBufferIndex++;
		bone->DiffuseSrvHeapIndex = 3;
		bone->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		bone->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
		bone->Roughness = 0.3f;

		auto shadowMat = std::make_unique<Material>();
		shadowMat->Name = "shadowMat";
		shadowMat->MatCBIndex = m_MatConstantBufferIndex++;
		shadowMat->DiffuseSrvHeapIndex = 3;
		shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
		shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
		shadowMat->Roughness = 0.0f;

		auto stone = std::make_unique<Material>();
		stone->Name = "stone";
		stone->MatCBIndex = m_MatConstantBufferIndex++;
		stone->DiffuseSrvHeapIndex = 0;
		stone->DiffuseAlbedo = XMFLOAT4(Colors::LightSteelBlue);
		stone->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
		stone->Roughness = 0.3f;

		auto tile = std::make_unique<Material>();
		tile->Name = "tile";
		tile->MatCBIndex = m_MatConstantBufferIndex++;
		tile->DiffuseSrvHeapIndex = 0;
		tile->DiffuseAlbedo = XMFLOAT4(Colors::LightGray);
		tile->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
		tile->Roughness = 0.2f;

		auto metal = std::make_unique<Material>();
		metal->Name = "metal";
		metal->MatCBIndex = m_MatConstantBufferIndex++;
		metal->DiffuseSrvHeapIndex = 0;
		metal->DiffuseAlbedo = XMFLOAT4(Colors::Silver);
		metal->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
		metal->Roughness = 0.005f;

		auto highlight = std::make_unique<Material>();
		highlight->Name = "highlight";
		highlight->MatCBIndex = m_MatConstantBufferIndex++;
		highlight->DiffuseSrvHeapIndex = 0;
		highlight->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.6f);
		highlight->FresnelR0 = XMFLOAT3(0.06f, 0.06f, 0.06f);
		highlight->Roughness = 0.0f;

		m_Materials["bricks"] = std::move(bricks);
		m_Materials["checkertile"] = std::move(checkertile);
		m_Materials["icemirror"] = std::move(icemirror);
		m_Materials["bone"] = std::move(bone);
		m_Materials["shadowMat"] = std::move(shadowMat);
		m_Materials["stone"] = std::move(stone);
		m_Materials["tile"] = std::move(tile);
		m_Materials["metal"] = std::move(metal);
		m_Materials["highlight"] = std::move(highlight);

		for (auto& e : m_Materials)
		{
			m_ImguiManager.SetMaterialsCount(e.second.get()->MatCBIndex + 1);
			m_ImguiManager.SetMaterialsName(e.second.get()->Name);
			m_ImguiManager.SetMaterialsAlbedo(e.second.get()->DiffuseAlbedo);
			m_ImguiManager.SetMaterialsFresnelR0(e.second.get()->FresnelR0);
			m_ImguiManager.SetMaterialsRoughness(e.second.get()->Roughness);
		}
	}

	void GraphicsClass::BuildRenderItems()
	{
		auto floorRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&floorRitem->World, XMMatrixScaling(3.0f, 3.0f, 3.0f));
		floorRitem->TexTransform = MathHelper::Identity4x4();
		floorRitem->GeoShapeName = "floor";
		floorRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		floorRitem->Mat = m_Materials["checkertile"].get();
		floorRitem->Geo = m_Geometries["roomGeo"].get();
		floorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		floorRitem->IndexCount = floorRitem->Geo->DrawArgs["floor"].IndexCount;
		floorRitem->StartIndexLocation = floorRitem->Geo->DrawArgs["floor"].StartIndexLocation;
		floorRitem->BaseVertexLocation = floorRitem->Geo->DrawArgs["floor"].BaseVertexLocation;
		m_RenderItemLayer[(int)RenderLayer::Opaque].push_back(floorRitem.get());
		m_ImguiManager.SetGeometryShapes(floorRitem->GeoShapeName, floorRitem->IsVisible);
		
		auto wallsRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallsRitem->World, XMMatrixScaling(3.0f, 3.0f, 3.0f));
		wallsRitem->TexTransform = MathHelper::Identity4x4();
		wallsRitem->GeoShapeName = "wall";
		wallsRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		wallsRitem->Mat = m_Materials["bricks"].get();
		wallsRitem->Geo = m_Geometries["roomGeo"].get();
		wallsRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallsRitem->IndexCount = wallsRitem->Geo->DrawArgs["wall"].IndexCount;
		wallsRitem->StartIndexLocation = wallsRitem->Geo->DrawArgs["wall"].StartIndexLocation;
		wallsRitem->BaseVertexLocation = wallsRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
		m_RenderItemLayer[(int)RenderLayer::Opaque].push_back(wallsRitem.get());
		m_ImguiManager.SetGeometryShapes(wallsRitem->GeoShapeName, wallsRitem->IsVisible);

		auto carRitem = std::make_unique<RenderItem>();
		XMStoreFloat3(&carRitem->WorldScaling, { 1.0f, 1.0f, 1.0f });
		XMStoreFloat3(&carRitem->WorldTranslation, { 12.0f, 3.0f, -14.0f });
		XMStoreFloat4x4(&carRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(12.0f, 3.0f, -14.0f));
		XMStoreFloat4x4(&carRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		carRitem->GeoName = "carGeo";
		carRitem->GeoShapeName = "car";
		carRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		carRitem->Mat = m_Materials["metal"].get();
		carRitem->Geo = m_Geometries[carRitem->GeoName].get();
		carRitem->DoPicking = true;
		carRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		carRitem->Bounds = carRitem->Geo->DrawArgs[carRitem->GeoShapeName].Bounds;
		carRitem->IndexCount = carRitem->Geo->DrawArgs[carRitem->GeoShapeName].IndexCount;
		carRitem->StartIndexLocation = carRitem->Geo->DrawArgs[carRitem->GeoShapeName].StartIndexLocation;
		carRitem->BaseVertexLocation = carRitem->Geo->DrawArgs[carRitem->GeoShapeName].BaseVertexLocation;
		m_RenderItemLayer[(int)RenderLayer::Opaque].push_back(carRitem.get());
		m_ImguiManager.SetModels(carRitem->GeoShapeName, carRitem->IsVisible);

		auto boxRitem = std::make_unique<RenderItem>();
		XMStoreFloat3(&boxRitem->WorldScaling, { 3.0f, 3.0f, 3.0f });
		XMStoreFloat3(&boxRitem->WorldTranslation, { -5.0f, 1.0f, -10.0f });
		XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(3.0f, 3.0f, 3.0f) * XMMatrixTranslation(-5.0f, 1.0f, -10.0f));
		XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		boxRitem->GeoName = "shapeGeo";
		boxRitem->GeoShapeName = "box";
		boxRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		boxRitem->Mat = m_Materials["tile"].get();
		boxRitem->Geo = m_Geometries[boxRitem->GeoName].get();
		boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		boxRitem->DoPicking = true;
		boxRitem->Bounds = boxRitem->Geo->DrawArgs[boxRitem->GeoShapeName].Bounds;
		boxRitem->IndexCount = boxRitem->Geo->DrawArgs[boxRitem->GeoShapeName].IndexCount;
		boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs[boxRitem->GeoShapeName].StartIndexLocation;
		boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs[boxRitem->GeoShapeName].BaseVertexLocation;
		m_RenderItemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
		m_ImguiManager.SetGeometryShapes(boxRitem->GeoShapeName, boxRitem->IsVisible);

		auto sphereRitem = std::make_unique<RenderItem>();
		XMStoreFloat3(&sphereRitem->WorldScaling, { 4.0f, 4.0f, 4.0f });
		XMStoreFloat3(&sphereRitem->WorldTranslation, { -1.0f, 4.0f, -18.0f });
		XMStoreFloat4x4(&sphereRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(-1.0f, 4.0f, -18.0f));
		XMStoreFloat4x4(&sphereRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		sphereRitem->GeoName = "shapeGeo";
		sphereRitem->GeoShapeName = "sphere";
		sphereRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		sphereRitem->Mat = m_Materials["bone"].get();
		sphereRitem->Geo = m_Geometries[sphereRitem->GeoName].get();
		sphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		sphereRitem->DoPicking = true;
		sphereRitem->Bounds = sphereRitem->Geo->DrawArgs[sphereRitem->GeoShapeName].Bounds;
		sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs[sphereRitem->GeoShapeName].IndexCount;
		sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs[sphereRitem->GeoShapeName].StartIndexLocation;
		sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs[sphereRitem->GeoShapeName].BaseVertexLocation;
		m_RenderItemLayer[(int)RenderLayer::Opaque].push_back(sphereRitem.get());
		m_ImguiManager.SetGeometryShapes(sphereRitem->GeoShapeName, sphereRitem->IsVisible);

		auto cylinderRitem = std::make_unique<RenderItem>();
		XMStoreFloat3(&cylinderRitem->WorldScaling, { 3.0f, 3.0f, 3.0f });
		XMStoreFloat3(&cylinderRitem->WorldTranslation, { 5.0f, 5.0f, -10.0f });
		XMStoreFloat4x4(&cylinderRitem->World, XMMatrixScaling(3.0f, 3.0f, 3.0f) * XMMatrixTranslation(5.0f, 5.0f, -10.0f));
		XMStoreFloat4x4(&cylinderRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		cylinderRitem->GeoName = "shapeGeo";
		cylinderRitem->GeoShapeName = "cylinder";
		cylinderRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		cylinderRitem->Mat = m_Materials["stone"].get();
		cylinderRitem->Geo = m_Geometries[cylinderRitem->GeoName].get();
		cylinderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		cylinderRitem->DoPicking = true;
		cylinderRitem->Bounds = cylinderRitem->Geo->DrawArgs[cylinderRitem->GeoShapeName].Bounds;
		cylinderRitem->IndexCount = cylinderRitem->Geo->DrawArgs[cylinderRitem->GeoShapeName].IndexCount;
		cylinderRitem->StartIndexLocation = cylinderRitem->Geo->DrawArgs[cylinderRitem->GeoShapeName].StartIndexLocation;
		cylinderRitem->BaseVertexLocation = cylinderRitem->Geo->DrawArgs[cylinderRitem->GeoShapeName].BaseVertexLocation;
		m_RenderItemLayer[(int)RenderLayer::Opaque].push_back(cylinderRitem.get());
		m_ImguiManager.SetGeometryShapes(cylinderRitem->GeoShapeName, cylinderRitem->IsVisible);

		auto reflectedFloorRitem = std::make_unique<RenderItem>();
		*reflectedFloorRitem = *floorRitem;
		reflectedFloorRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedFloorRitem->GeoShapeName = "reflFloor";
		reflectedFloorRitem->FrustumTest = false;
		reflectedFloorRitem->ReflectionSource = floorRitem.get();
		m_RenderItemLayer[(int)RenderLayer::Reflected].push_back(reflectedFloorRitem.get());

		auto reflectedCarRitem = std::make_unique<RenderItem>();
		*reflectedCarRitem = *carRitem;
		reflectedCarRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedCarRitem->GeoShapeName = "reflCar";
		reflectedCarRitem->FrustumTest = false;
		reflectedCarRitem->ReflectionSource = carRitem.get();
		m_RenderItemLayer[(int)RenderLayer::Reflected].push_back(reflectedCarRitem.get());

		auto reflectedBoxRitem = std::make_unique<RenderItem>();
		*reflectedBoxRitem = *boxRitem;
		reflectedBoxRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedBoxRitem->GeoShapeName = "reflBox";
		reflectedBoxRitem->FrustumTest = false;
		reflectedBoxRitem->ReflectionSource = boxRitem.get();
		m_RenderItemLayer[(int)RenderLayer::Reflected].push_back(reflectedBoxRitem.get());

		auto reflectedSphereRitem = std::make_unique<RenderItem>();
		*reflectedSphereRitem = *sphereRitem;
		reflectedSphereRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedSphereRitem->GeoShapeName = "reflSphere";
		reflectedSphereRitem->FrustumTest = false;
		reflectedSphereRitem->ReflectionSource = sphereRitem.get();
		m_RenderItemLayer[(int)RenderLayer::Reflected].push_back(reflectedSphereRitem.get());

		auto reflectedCylinderRitem = std::make_unique<RenderItem>();
		*reflectedCylinderRitem = *cylinderRitem;
		reflectedCylinderRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedCylinderRitem->GeoShapeName = "reflCylinder";
		reflectedCylinderRitem->FrustumTest = false;
		reflectedCylinderRitem->ReflectionSource = cylinderRitem.get();
		m_RenderItemLayer[(int)RenderLayer::Reflected].push_back(reflectedCylinderRitem.get());
		
		auto shadowedCarRitem = std::make_unique<RenderItem>();
		*shadowedCarRitem = *carRitem;
		shadowedCarRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		shadowedCarRitem->Mat = m_Materials["shadowMat"].get();
		shadowedCarRitem->GeoShapeName = "shadowCar";
		shadowedCarRitem->ShadowSource = carRitem.get();
		shadowedCarRitem->ShadowIndex = 0;
		m_RenderItemLayer[(int)RenderLayer::Shadow].push_back(shadowedCarRitem.get());

		auto shadowedBoxRitem = std::make_unique<RenderItem>();
		*shadowedBoxRitem = *boxRitem;
		shadowedBoxRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		shadowedBoxRitem->Mat = m_Materials["shadowMat"].get();
		shadowedBoxRitem->GeoShapeName = "shadowBox";
		shadowedBoxRitem->ShadowSource = boxRitem.get();
		shadowedBoxRitem->ShadowIndex = 0;
		m_RenderItemLayer[(int)RenderLayer::Shadow].push_back(shadowedBoxRitem.get());

		auto shadowedSphereRitem = std::make_unique<RenderItem>();
		*shadowedSphereRitem = *sphereRitem;
		shadowedSphereRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		shadowedSphereRitem->Mat = m_Materials["shadowMat"].get();
		shadowedSphereRitem->GeoShapeName = "shadowSphere";
		shadowedSphereRitem->ShadowSource = sphereRitem.get();
		shadowedSphereRitem->ShadowIndex = 0;
		m_RenderItemLayer[(int)RenderLayer::Shadow].push_back(shadowedSphereRitem.get());

		auto shadowedCylinderRitem = std::make_unique<RenderItem>();
		*shadowedCylinderRitem = *cylinderRitem;
		shadowedCylinderRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		shadowedCylinderRitem->Mat = m_Materials["shadowMat"].get();
		shadowedCylinderRitem->GeoShapeName = "shadowCylinder";
		shadowedCylinderRitem->ShadowSource = cylinderRitem.get();
		shadowedCylinderRitem->ShadowIndex = 0;
		m_RenderItemLayer[(int)RenderLayer::Shadow].push_back(shadowedCylinderRitem.get());

		auto reflectedShadowedCarRitem = std::make_unique<RenderItem>();
		*reflectedShadowedCarRitem = *shadowedCarRitem;
		reflectedShadowedCarRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedShadowedCarRitem->GeoShapeName = "reflShadowCar";
		reflectedShadowedCarRitem->FrustumTest = false;
		reflectedShadowedCarRitem->ReflectionSource = shadowedCarRitem.get();
		m_RenderItemLayer[(int)RenderLayer::ShadowReflected].push_back(reflectedShadowedCarRitem.get());

		auto reflectedShadowedBoxRitem = std::make_unique<RenderItem>();
		*reflectedShadowedBoxRitem = *shadowedBoxRitem;
		reflectedShadowedBoxRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedShadowedBoxRitem->GeoShapeName = "reflShadowBox";
		reflectedShadowedBoxRitem->FrustumTest = false;
		reflectedShadowedBoxRitem->ReflectionSource = shadowedBoxRitem.get();
		m_RenderItemLayer[(int)RenderLayer::ShadowReflected].push_back(reflectedShadowedBoxRitem.get());

		auto reflectedShadowedSphereRitem = std::make_unique<RenderItem>();
		*reflectedShadowedSphereRitem = *shadowedSphereRitem;
		reflectedShadowedSphereRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedShadowedSphereRitem->GeoShapeName = "reflShadowSphere";
		reflectedShadowedSphereRitem->FrustumTest = false;
		reflectedShadowedSphereRitem->ReflectionSource = shadowedSphereRitem.get();
		m_RenderItemLayer[(int)RenderLayer::ShadowReflected].push_back(reflectedShadowedSphereRitem.get());

		auto reflectedShadowedCylinderRitem = std::make_unique<RenderItem>();
		*reflectedShadowedCylinderRitem = *shadowedCylinderRitem;
		reflectedShadowedCylinderRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedShadowedCylinderRitem->GeoShapeName = "reflShadowCylinder";
		reflectedShadowedCylinderRitem->FrustumTest = false;
		reflectedShadowedCylinderRitem->ReflectionSource = shadowedCylinderRitem.get();
		m_RenderItemLayer[(int)RenderLayer::ShadowReflected].push_back(reflectedShadowedCylinderRitem.get());

		auto mirrorRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&mirrorRitem->World, XMMatrixScaling(3.0f, 3.0f, 3.0f));
		mirrorRitem->TexTransform = MathHelper::Identity4x4();
		mirrorRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		mirrorRitem->Mat = m_Materials["icemirror"].get();
		mirrorRitem->Geo = m_Geometries["roomGeo"].get();
		mirrorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		mirrorRitem->IndexCount = mirrorRitem->Geo->DrawArgs["mirror"].IndexCount;
		mirrorRitem->StartIndexLocation = mirrorRitem->Geo->DrawArgs["mirror"].StartIndexLocation;
		mirrorRitem->BaseVertexLocation = mirrorRitem->Geo->DrawArgs["mirror"].BaseVertexLocation;
		mirrorRitem->GeoShapeName = "mirror";
		m_RenderItemLayer[(int)RenderLayer::Mirrors].push_back(mirrorRitem.get());
		m_RenderItemLayer[(int)RenderLayer::Transparent].push_back(mirrorRitem.get());

		auto pickedRitem = std::make_unique<RenderItem>();
		pickedRitem->World = MathHelper::Identity4x4();
		pickedRitem->TexTransform = MathHelper::Identity4x4();
		pickedRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		pickedRitem->Mat = m_Materials["highlight"].get();
		pickedRitem->Geo = m_Geometries["shapeGeo"].get();
		pickedRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		// Picked triangle is not visible until one is picked.
		pickedRitem->IsVisible = false;
		// DrawCall parameters are filled out when a triangle is picked.
		pickedRitem->IndexCount = 0;
		pickedRitem->StartIndexLocation = 0;
		pickedRitem->BaseVertexLocation = 0;
		m_PickedRenderItem = pickedRitem.get();
		m_RenderItemLayer[(int)RenderLayer::Highlight].push_back(m_PickedRenderItem);

		m_AllRenderItems.push_back(std::move(floorRitem));
		m_AllRenderItems.push_back(std::move(wallsRitem));
		m_AllRenderItems.push_back(std::move(carRitem));
		m_AllRenderItems.push_back(std::move(boxRitem));
		m_AllRenderItems.push_back(std::move(sphereRitem));
		m_AllRenderItems.push_back(std::move(cylinderRitem));
		m_AllRenderItems.push_back(std::move(reflectedFloorRitem));
		m_AllRenderItems.push_back(std::move(reflectedCarRitem));
		m_AllRenderItems.push_back(std::move(reflectedBoxRitem));
		m_AllRenderItems.push_back(std::move(reflectedSphereRitem));
		m_AllRenderItems.push_back(std::move(reflectedCylinderRitem));
		m_AllRenderItems.push_back(std::move(shadowedCarRitem));
		m_AllRenderItems.push_back(std::move(shadowedBoxRitem));
		m_AllRenderItems.push_back(std::move(shadowedSphereRitem));
		m_AllRenderItems.push_back(std::move(shadowedCylinderRitem));
		m_AllRenderItems.push_back(std::move(reflectedShadowedCarRitem));
		m_AllRenderItems.push_back(std::move(reflectedShadowedBoxRitem));
		m_AllRenderItems.push_back(std::move(reflectedShadowedSphereRitem));
		m_AllRenderItems.push_back(std::move(reflectedShadowedCylinderRitem));
		m_AllRenderItems.push_back(std::move(mirrorRitem));
		m_AllRenderItems.push_back(std::move(pickedRitem));
	}
	
	void GraphicsClass::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
	{
		UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
		UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

		auto objectCB = m_CurrentFrameResource->ObjectCB->Resource();
		auto matCB = m_CurrentFrameResource->MaterialCB->Resource();

		// For each render item...
		for (size_t i = 0; i < ritems.size(); ++i)
		{
			auto ri = ritems[i];

			if (ri->IsVisible == false || ri->FrustumTestResult == false)
				continue;

			D3D12_VERTEX_BUFFER_VIEW vertexBufferView = ri->Geo->VertexBufferView();
			cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
			D3D12_INDEX_BUFFER_VIEW indexBufferView = ri->Geo->IndexBufferView();
			cmdList->IASetIndexBuffer(&indexBufferView);
			cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

			CD3DX12_GPU_DESCRIPTOR_HANDLE tex(m_ShaderResourceViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			tex.Offset(ri->Mat->DiffuseSrvHeapIndex, m_CbvSrvDescriptorSize);

			D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = 
				objectCB->GetGPUVirtualAddress() + ri->ObjConstantBufferIndex * objCBByteSize;
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = 
				matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

			cmdList->SetGraphicsRootDescriptorTable(0, tex);
			cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
			cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

			cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
		}
	}

	void GraphicsClass::UpdateImGuiData()
	{
		for (auto ri : m_RenderItemLayer[(int)RenderLayer::Opaque])
		{
			if (ri->IsPicked)
			{
				ri->WorldScaling = m_ImguiManager.GetItemScaling();
				ri->WorldRotation.x = XMConvertToRadians(m_ImguiManager.GetItemRotation().x);
				ri->WorldRotation.y = XMConvertToRadians(m_ImguiManager.GetItemRotation().y);
				ri->WorldRotation.z = XMConvertToRadians(m_ImguiManager.GetItemRotation().z);
				ri->WorldTranslation = m_ImguiManager.GetItemTranslation();

				XMMATRIX world = XMMatrixIdentity();
				world *= XMMatrixScaling(ri->WorldScaling.x, ri->WorldScaling.y, ri->WorldScaling.z);
				world *= XMMatrixRotationX(ri->WorldRotation.x) * XMMatrixRotationY(ri->WorldRotation.y) * XMMatrixRotationZ(ri->WorldRotation.z);
				world *= XMMatrixTranslation(ri->WorldTranslation.x, ri->WorldTranslation.y, ri->WorldTranslation.z);
				XMStoreFloat4x4(&ri->World, world);

				ri->Mat = m_Materials[m_ImguiManager.GetItemMaterial()].get();

				ri->NumFramesDirty = gNumFrameResources;
				ri->Mat->NumFramesDirty = gNumFrameResources;

				m_PickedRenderItem->World = ri->World;
				m_PickedRenderItem->NumFramesDirty = gNumFrameResources;

				for (auto& e : m_RenderItemLayer[(int)RenderLayer::Reflected])
				{
					if (e->ReflectionSource == ri)
						e->Mat = ri->Mat;
				}
			}
		}
	}

	void GraphicsClass::UpdateLights()
	{
		m_MainPassConstantBuffer.AmbientLight = m_ImguiManager.GetAmbientLight();
		m_ImguiManager.UpdateLights();
		for (int i = 0; i < MaxLights; i++)
			m_MainPassConstantBuffer.Lights[i] = m_ImguiManager.GetLights(i);
	}

	void GraphicsClass::UpdateSceneData()
	{
		// Update objects
		int i = 0;
		for (auto ri : m_RenderItemLayer[(int)RenderLayer::Opaque])
		{
			if (m_ImguiManager.GetGeometryShapes().contains(ri->GeoShapeName))
			{
				ri->IsVisible = m_ImguiManager.GetGeometryShapes()[ri->GeoShapeName];
				for (auto& e : m_AllRenderItems)
				{
					if (e->ShadowSource == ri)
						e->IsVisible = ri->IsVisible;
					if (e->ReflectionSource == ri)
						e->IsVisible = ri->IsVisible;
				}
			}
			else
			{
				ri->IsVisible = m_ImguiManager.GetModels()[ri->GeoShapeName];
				for (auto& e : m_AllRenderItems)
				{
					if (e->ShadowSource == ri)
						e->IsVisible = ri->IsVisible;
					if (e->ReflectionSource == ri)
						e->IsVisible = ri->IsVisible;
				}
			}

			if (ri->GeoShapeName == m_ImguiManager.GetShapeEraseName())
			{
				for (auto& e : m_AllRenderItems)
				{
					if (e->ShadowSource == ri)
						e->IsVisible = false;
					if (e->ReflectionSource == ri)
						e->IsVisible = false;
				}
				if (ri->IsPicked)
					m_PickedRenderItem->IsVisible = false;

				m_RenderItemLayer->erase(m_RenderItemLayer->begin() + i, m_RenderItemLayer->begin() + i + 1);
				m_ImguiManager.EraseShape(m_ImguiManager.GetShapeEraseName());
				m_ImguiManager.SetShapeEraseName("\0");
				--m_ObjConstantBufferIndex;

				break;
			}
			++i;
		}

		//Update materials
		i = 0;
		for (auto& e : m_Materials)
		{
			e.second.get()->DiffuseAlbedo = m_ImguiManager.GetMaterialsAlbedo()[i];
			e.second.get()->FresnelR0 = m_ImguiManager.GetMaterialsFresnelR0()[i];
			e.second.get()->Roughness = m_ImguiManager.GetMaterialsRoughness()[i];
			e.second.get()->NumFramesDirty = gNumFrameResources;
			++i;
		}
	}

	void GraphicsClass::AddShape()
	{
		ThrowIfFailed(m_CommandList->Reset(m_DirectCommandListAllocator.Get(), nullptr));

		GeometryGenerator geoGen;
		GeometryGenerator::MeshData shape;
		std::string geoShapeName;
		std::string num = std::to_string(++m_AddedShapesCount);
		std::string matName;

		AddShapeData addShapeData = m_ImguiManager.GetAddShape();

		if (addShapeData.ShapeGeo == Geometry::Box)
		{
			shape = geoGen.CreateBox(addShapeData.BoxWidth, addShapeData.BoxHeight,
				addShapeData.BoxDepth, addShapeData.BoxNumSubdivisions);
			geoShapeName = "box" + num;
			matName = addShapeData.BoxMaterial;
		}
		else if (addShapeData.ShapeGeo == Geometry::Grid)
		{
			shape = geoGen.CreateGrid(addShapeData.GridWidth, addShapeData.GridDepth,
				addShapeData.GridM, addShapeData.GridN);
			geoShapeName = "grid" + num;
			matName = addShapeData.GridMaterial;
		}
		else if (addShapeData.ShapeGeo == Geometry::Sphere)
		{
			shape = geoGen.CreateSphere(addShapeData.SphereRadius,
				addShapeData.SphereSliceCount, addShapeData.SphereStackCount);
			geoShapeName = "sphere" + num;
			matName = addShapeData.SphereMaterial;
		}
		else if (addShapeData.ShapeGeo == Geometry::Geosphere)
		{
			shape = geoGen.CreateGeosphere(addShapeData.GeosphereRadius,
				addShapeData.GeosphereNumSubdivisions);
			geoShapeName = "geosphere" + num;
			matName = addShapeData.GeosphereMaterial;
		}
		else if (addShapeData.ShapeGeo == Geometry::Cylinder)
		{
			shape = geoGen.CreateCylinder(addShapeData.CylinderBottomRadius,
				addShapeData.CylinderTopRadius, addShapeData.CylinderHeight,
				addShapeData.CylinderSliceCount, addShapeData.CylinderStackCount);
			geoShapeName = "cylinder" + num;
			matName = addShapeData.CylinderMaterial;
		}

		UINT shapeVertexOffset = 0;
		UINT shapeIndexOffset = 0;

		auto totalVertexCount = shape.Vertices.size();

		std::vector<Vertex> vertices(totalVertexCount);

		XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
		XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

		XMVECTOR vMin = XMLoadFloat3(&vMinf3);
		XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

		UINT k = 0;
		for (size_t i = 0; i < shape.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = shape.Vertices[i].Position;
			vertices[k].Normal = shape.Vertices[i].Normal;

			XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

			vertices[i].TexC = { 0.0f, 0.0f };

			vMin = XMVectorMin(vMin, P);
			vMax = XMVectorMax(vMax, P);
		}

		BoundingBox shapeBounds;
		XMStoreFloat3(&shapeBounds.Center, 0.5f * (vMin + vMax));
		XMStoreFloat3(&shapeBounds.Extents, 0.5f * (vMax - vMin));

		std::vector<std::uint16_t> indices;
		indices.insert(indices.end(), std::begin(shape.GetIndices16()), std::end(shape.GetIndices16()));

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

		auto geo = std::make_unique<MeshGeometry>();

		std::string geoName = "addedShapeGeo" + num;
		geo->Name = geoName;

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
			m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
			m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R16_UINT;
		geo->IndexBufferByteSize = ibByteSize;

		SubmeshGeometry shapeSubmesh;
		shapeSubmesh.IndexCount = (UINT)shape.Indices32.size();
		shapeSubmesh.StartIndexLocation = shapeIndexOffset;
		shapeSubmesh.BaseVertexLocation = shapeVertexOffset;
		shapeSubmesh.Bounds = shapeBounds;

		geo->DrawArgs[geoShapeName] = shapeSubmesh;

		m_Geometries[geo->Name] = std::move(geo);

		auto shapeRitem = std::make_unique<RenderItem>();
		XMStoreFloat3(&shapeRitem->WorldScaling, { 1.0f, 1.0f, 1.0f });
		XMStoreFloat3(&shapeRitem->WorldTranslation, { addShapeData.Pos.x,
			addShapeData.Pos.y, addShapeData.Pos.z });
		XMStoreFloat4x4(&shapeRitem->World, XMMatrixTranslation(shapeRitem->WorldTranslation.x,
			shapeRitem->WorldTranslation.y, shapeRitem->WorldTranslation.z));
		XMStoreFloat4x4(&shapeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		shapeRitem->GeoName = geoName;
		shapeRitem->GeoShapeName = geoShapeName;
		shapeRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		shapeRitem->Mat = m_Materials[matName].get();
		shapeRitem->Geo = m_Geometries[shapeRitem->GeoName].get();
		shapeRitem->DoPicking = true;
		shapeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		shapeRitem->Bounds = shapeRitem->Geo->DrawArgs[shapeRitem->GeoShapeName].Bounds;
		shapeRitem->IndexCount = shapeRitem->Geo->DrawArgs[shapeRitem->GeoShapeName].IndexCount;
		shapeRitem->StartIndexLocation = shapeRitem->Geo->DrawArgs[shapeRitem->GeoShapeName].StartIndexLocation;
		shapeRitem->BaseVertexLocation = shapeRitem->Geo->DrawArgs[shapeRitem->GeoShapeName].BaseVertexLocation;
		m_RenderItemLayer[(int)RenderLayer::Opaque].push_back(shapeRitem.get());
		m_ImguiManager.SetGeometryShapes(shapeRitem->GeoShapeName, shapeRitem->IsVisible);

		auto reflectedShapeRitem = std::make_unique<RenderItem>();
		*reflectedShapeRitem = *shapeRitem;
		reflectedShapeRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedShapeRitem->GeoShapeName = "refl" + geoShapeName;
		reflectedShapeRitem->FrustumTest = false;
		reflectedShapeRitem->ReflectionSource = shapeRitem.get();
		m_RenderItemLayer[(int)RenderLayer::Reflected].push_back(reflectedShapeRitem.get());

		auto shadowedShapeRitem = std::make_unique<RenderItem>();
		*shadowedShapeRitem = *shapeRitem;
		shadowedShapeRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		shadowedShapeRitem->Mat = m_Materials["shadowMat"].get();
		shadowedShapeRitem->GeoShapeName = "shadow" + geoShapeName;
		shadowedShapeRitem->ShadowSource = shapeRitem.get();
		shadowedShapeRitem->ShadowIndex = 0;
		m_RenderItemLayer[(int)RenderLayer::Shadow].push_back(shadowedShapeRitem.get());

		auto reflectedShadowedShapeRitem = std::make_unique<RenderItem>();
		*reflectedShadowedShapeRitem = *shadowedShapeRitem;
		reflectedShadowedShapeRitem->ObjConstantBufferIndex = m_ObjConstantBufferIndex++;
		reflectedShadowedShapeRitem->GeoShapeName = "reflShadow" + geoShapeName;
		reflectedShadowedShapeRitem->FrustumTest = false;
		reflectedShadowedShapeRitem->ReflectionSource = shadowedShapeRitem.get();
		m_RenderItemLayer[(int)RenderLayer::ShadowReflected].push_back(reflectedShadowedShapeRitem.get());

		m_AllRenderItems.push_back(std::move(shapeRitem));
		m_AllRenderItems.push_back(std::move(reflectedShapeRitem));
		m_AllRenderItems.push_back(std::move(shadowedShapeRitem));
		m_AllRenderItems.push_back(std::move(reflectedShadowedShapeRitem));

		m_FrameResources.clear();

		for (int i = 0; i < gNumFrameResources; ++i)
		{
			m_FrameResources.push_back(std::make_unique<FrameResource>(m_d3dDevice.Get(),
				2, (UINT)m_AllRenderItems.size(), (UINT)m_Materials.size()));
		}

		for (int i = 0; i < (int)RenderLayer::Count; i++)
		{
			for (auto e : m_RenderItemLayer[i])
			{
				e->NumFramesDirty = gNumFrameResources;
				e->Mat->NumFramesDirty = gNumFrameResources;
			}
		}

		ThrowIfFailed(m_CommandList->Close());
		ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
		m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
		FlushCommandQueue();

		m_ImguiManager.AddShapeFlag(false);
	}

	void GraphicsClass::AddMaterial()
	{
		AddMaterialData addMaterialData = m_ImguiManager.GetAddMaterial();

		auto mat = std::make_unique<Material>();
		mat->Name = addMaterialData.Name;
		mat->MatCBIndex = m_MatConstantBufferIndex++;
		mat->DiffuseSrvHeapIndex = 0;
		mat->DiffuseAlbedo = addMaterialData.Albedo;
		mat->FresnelR0 = addMaterialData.FresnelR0;
		mat->Roughness = addMaterialData.Roughness;

		m_ImguiManager.SetMaterialsCount(mat->MatCBIndex + 1);
		m_ImguiManager.SetMaterialsName(mat->Name);
		m_ImguiManager.SetMaterialsAlbedo(mat->DiffuseAlbedo);
		m_ImguiManager.SetMaterialsFresnelR0(mat->FresnelR0);
		m_ImguiManager.SetMaterialsRoughness(mat->Roughness);

		m_Materials[addMaterialData.Name] = std::move(mat);

		m_FrameResources.clear();

		for (int i = 0; i < gNumFrameResources; ++i)
		{
			m_FrameResources.push_back(std::make_unique<FrameResource>(m_d3dDevice.Get(),
				2, (UINT)m_AllRenderItems.size(), (UINT)m_Materials.size()));
		}
		
		for (int i = 0; i < (int)RenderLayer::Count; i++)
		{
			for (auto e : m_RenderItemLayer[i])
			{
				e->NumFramesDirty = gNumFrameResources;
				e->Mat->NumFramesDirty = gNumFrameResources;
			}
		}

		m_ImguiManager.CreateMaterialFlag(false);
	}

	void GraphicsClass::Pick(int sx, int sy)
	{
		bool pick = false;

		XMFLOAT4X4 P = m_Camera.GetProj4x4f();

		// Compute picking ray in view space.
		float vx = (+2.0f * sx / m_ClientWidth - 1.0f) / P(0, 0);
		float vy = (-2.0f * sy / m_ClientHeight + 1.0f) / P(1, 1);

		XMMATRIX V = m_Camera.GetView();
		XMVECTOR detV = XMMatrixDeterminant(V);
		XMMATRIX invView = XMMatrixInverse(&detV, V);

		// Assume nothing is picked to start, so the picked render-item is invisible.
		m_PickedRenderItem->IsVisible = false;

		// Check if we picked an opaque render item.  A real app might keep a separate "picking list"
		// of objects that can be selected.   
		for (auto ri : m_RenderItemLayer[(int)RenderLayer::Opaque])
		{
			auto geo = ri->Geo;

			ri->IsPicked = false;

			// Skip invisible render-items.
			if (ri->IsVisible == false || ri->DoPicking == false)
				continue;

			XMMATRIX W = XMLoadFloat4x4(&ri->World);
			XMVECTOR detW = XMMatrixDeterminant(W);
			XMMATRIX invWorld = XMMatrixInverse(&detW, W);

			// Tranform ray to vi space of Mesh.
			XMMATRIX toLocal = XMMatrixMultiply(invView, invWorld);

			// Ray definition in view space.
			XMVECTOR rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
			XMVECTOR rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);

			rayOrigin = XMVector3TransformCoord(rayOrigin, toLocal);
			rayDir = XMVector3TransformNormal(rayDir, toLocal);

			// Make the ray direction unit length for the intersection tests.
			rayDir = XMVector3Normalize(rayDir);

			// If we hit the bounding box of the Mesh, then we might have picked a Mesh triangle,
			// so do the ray/triangle tests.
			//
			// If we did not hit the bounding box, then it is impossible that we hit 
			// the Mesh, so do not waste effort doing ray/triangle tests.
			float tmin = 0.0f;

			if (ri->Bounds.Intersects(rayOrigin, rayDir, tmin))
			{
				// NOTE: For the demo, we know what to cast the vertex/index data to.  If we were mixing
				// formats, some metadata would be needed to figure out what to cast it to.
				auto vertices = (Vertex*)geo->VertexBufferCPU->GetBufferPointer();
				auto indices = (std::uint16_t*)geo->IndexBufferCPU->GetBufferPointer();
				UINT triCount = ri->IndexCount / 3;

				// Find the nearest ray/triangle intersection.
				tmin = MathHelper::Infinity;
				for (UINT i = 0; i < triCount; ++i)
				{
					// Indices for this triangle.
					UINT i0 = indices[i * 3 + 0];
					UINT i1 = indices[i * 3 + 1];
					UINT i2 = indices[i * 3 + 2];

					// Vertices for this triangle.
					XMVECTOR v0 = XMLoadFloat3(&vertices[i0].Pos);
					XMVECTOR v1 = XMLoadFloat3(&vertices[i1].Pos);
					XMVECTOR v2 = XMLoadFloat3(&vertices[i2].Pos);

					// We have to iterate over all the triangles in order to find the nearest intersection.
					float t = 0.0f;
					if (TriangleTests::Intersects(rayOrigin, rayDir, v0, v1, v2, t))
					{
						if (t < tmin)
						{
							// This is the new nearest picked triangle.
							tmin = t;
							UINT pickedTriangle = i;

							//m_RenderItemLayer[(int)RenderLayer::Opaque].at(i)->Picked = true;
							ri->IsPicked = true;

							m_PickedRenderItem->IsVisible = true;
							m_PickedRenderItem->Geo = m_Geometries[ri->GeoName].get();
							m_PickedRenderItem->IndexCount = ri->Geo->DrawArgs[ri->GeoShapeName].IndexCount;
							m_PickedRenderItem->BaseVertexLocation = ri->Geo->DrawArgs[ri->GeoShapeName].BaseVertexLocation;
							// Offset to the picked triangle in the mesh index buffer.
							m_PickedRenderItem->StartIndexLocation = ri->Geo->DrawArgs[ri->GeoShapeName].StartIndexLocation;
							// Picked render item needs same world matrix as object picked.
							m_PickedRenderItem->World = ri->World;
							m_PickedRenderItem->NumFramesDirty = gNumFrameResources;
							m_ImguiManager.UpdateItems(ri->IsPicked, ri->GeoShapeName, ri->Mat->Name,
								ri->WorldScaling, ri->WorldRotation, ri->WorldTranslation);
							pick = true;
						}
					}
				}
			}
		}
		if (!pick)
			m_ImguiManager.UpdateItems(false);
	}

	void GraphicsClass::MoveRenderItem(int sx, int sy, int sz)
	{
		for (auto ri : m_RenderItemLayer[(int)RenderLayer::Opaque])
		{
			if (ri->IsPicked)
			{
				DirectX::XMFLOAT3 translation = ri->WorldTranslation;

				if (sx < m_LastMousePos.x)
					translation.x -= 0.05f;
				if (sx > m_LastMousePos.x)
					translation.x += 0.05f;
				if (sy < m_LastMousePos.y)
					translation.z += 0.05f;
				if (sy > m_LastMousePos.y)
					translation.z -= 0.05f;
				if (sz < 0)
					translation.y -= 0.1f;
				if (sz > 0)
					translation.y += 0.1f;

				ri->WorldTranslation = translation;

				XMMATRIX world = XMMatrixIdentity();
				world *= XMMatrixScaling(ri->WorldScaling.x, ri->WorldScaling.y, ri->WorldScaling.z);
				world *= XMMatrixRotationX(ri->WorldRotation.x) * XMMatrixRotationY(ri->WorldRotation.y) * XMMatrixRotationZ(ri->WorldRotation.z);
				world *= XMMatrixTranslation(ri->WorldTranslation.x, ri->WorldTranslation.y, ri->WorldTranslation.z);
				XMStoreFloat4x4(&ri->World, world);

				ri->NumFramesDirty = gNumFrameResources;
				ri->IsPicked = false;

				Pick(sx, sy);
			}
		}
	}

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GraphicsClass::GetStaticSamplers()
	{

		// Applications usually only need a handful of samplers.  So just define them all up front
		// and keep them available as part of the root signature.  
		const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
			0, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

		const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
			1, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

		const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
			2, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

		const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
			3, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
			4, // shaderRegister
			D3D12_FILTER_ANISOTROPIC, // filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
			0.0f,                             // mipLODBias
			8);                               // maxAnisotropy

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
			5, // shaderRegister
			D3D12_FILTER_ANISOTROPIC, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
			0.0f,                              // mipLODBias
			8);                                // maxAnisotropy

		return {
			pointWrap, pointClamp,
			linearWrap, linearClamp,
			anisotropicWrap, anisotropicClamp };
	}
}
