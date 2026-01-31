#include <Windows.h>

// D3D 사용에 필요한 라이브러리들을 링크합니다.
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

// D3D 사용에 필요한 헤더파일들을 포함합니다.
#include <d3d11.h>
#include <d3dcompiler.h>

// ImGui 관련 헤더파일들을 포함합니다.
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

// 1. Define the triangle vertices
struct FVertexSimple
{
    float x, y, z;    // Position
    float r, g, b, a; // Color
};
// Structure for a 3D vector
struct FVector
{
    float x, y, z;

    FVector(float _x = 0, float _y = 0, float _z = 0)
        : x(_x), y(_y), z(_z) {
    }

    // --- 기본 연산자 ---
    FVector operator+(const FVector& rhs) const { return FVector(x + rhs.x, y + rhs.y, z + rhs.z); }
    FVector operator-(const FVector& rhs) const { return FVector(x - rhs.x, y - rhs.y, z - rhs.z); }
    FVector operator*(float s) const { return FVector(x * s, y * s, z * s); }
    FVector operator/(float s) const { return FVector(x / s, y / s, z / s); }

    FVector& operator+=(const FVector& rhs) { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
    FVector& operator-=(const FVector& rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this; }
    FVector& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    // --- 물리/기하 연산 ---

    // 1. 내적 (Dot Product): 두 벡터 사이의 각도나 투영 길이를 구할 때 사용
    float Dot(const FVector& rhs) const {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    // 2. 외적 (Cross Product): 두 벡터에 수직인 벡터(법선 벡터)를 구할 때 사용
    FVector Cross(const FVector& rhs) const {
        return FVector(
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x
        );
    }

    // 3. 길이의 제곱: 거리 비교 시 루트 연산을 피하기 위해 사용 (성능 최적화)
    float SizeSquared() const {
        return x * x + y * y + z * z;
    }

    // 4. 실제 길이 (Magnitude)
    float Size() const {
        return sqrtf(SizeSquared());
    }

    // 5. 정규화 (Normalize): 방향은 유지하고 길이를 1로 만듦
    FVector GetSafeNormal() const {
        float s = Size();
        if (s > 0.0001f) return *this * (1.0f / s);
        return FVector(0, 0, 0);
    }
};

bool EnableGravity = false;           // 중력 켜짐/꺼짐 상태
float GravityAcceleration = -9.8f;    // 중력 가속도 (Y 방향 아래로)

#include "Sphere.h"

class URenderer
{
public:
    // Direct3D 11 장치(Device)와 장치 컨텍스트(Device Context) 및 스왑 체인(Swap Chain)을 관리하기 위한 포인터들
    ID3D11Device* Device = nullptr; // GPU와 통신하기 위한 Direct3D 장치
    ID3D11DeviceContext* DeviceContext = nullptr; // GPU 명령 실행을 담당하는 컨텍스트
    IDXGISwapChain* SwapChain = nullptr; // 프레임 버퍼를 교체하는 데 사용되는 스왑 체인

    // 렌더링에 필요한 리소스 및 상태를 관리하기 위한 변수들
    ID3D11Texture2D* FrameBuffer = nullptr; // 화면 출력용 텍스처
    ID3D11RenderTargetView* FrameBufferRTV = nullptr; // 텍스처를 렌더 타겟으로 사용하는 뷰
    ID3D11RasterizerState* RasterizerState = nullptr; // 래스터라이저 상태(컬링, 채우기 모드 등 정의)
    ID3D11Buffer* ConstantBuffer = nullptr; // 쉐이더에 데이터를 전달하기 위한 상수 버퍼

    FLOAT ClearColor[4] = { 0.025f, 0.025f, 0.025f, 1.0f }; // 화면을 초기화(clear)할 때 사용할 색상 (RGBA)
    D3D11_VIEWPORT ViewportInfo; // 렌더링 영역을 정의하는 뷰포트 정보

    ID3D11VertexShader* SimpleVertexShader; // 정점 쉐이더
    ID3D11PixelShader* SimplePixelShader;   // 픽셀 쉐이더
    ID3D11InputLayout* SimpleInputLayout;   // IA입력 레이아웃
    unsigned int Stride;
    ID3D11Buffer* VertexBufferSphere = nullptr;
    UINT          NumVerticesSphere = 0;
    struct FConstants
    {
        FVector Offset;     // 위치
        float   Scale;      // 각 공의 스케일 팩터
        float   Pad[3];     // 16바이트 정렬 맞추기 위해 패딩
    };

public:
    // 렌더러 초기화 함수
    void Create(HWND hWindow)
    {
        // Direct3D 장치 및 스왑 체인 생성
        CreateDeviceAndSwapChain(hWindow);

        // 프레임 버퍼 생성
        CreateFrameBuffer();

        // 래스터라이저 상태 생성
        CreateRasterizerState();

        // 깊이 스텐실 버퍼 및 블렌드 상태는 이 코드에서는 다루지 않음
    }

    // Direct3D 장치 및 스왑 체인을 생성하는 함수
    void CreateDeviceAndSwapChain(HWND hWindow)
    {
        // 지원하는 Direct3D 기능 레벨을 정의
        D3D_FEATURE_LEVEL featurelevels[] = { D3D_FEATURE_LEVEL_11_0 };

        // 스왑 체인 설정 구조체 초기화
        DXGI_SWAP_CHAIN_DESC swapchaindesc = {};
        swapchaindesc.BufferDesc.Width = 0; // 창 크기에 맞게 자동으로 설정
        swapchaindesc.BufferDesc.Height = 0; // 창 크기에 맞게 자동으로 설정
        swapchaindesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 색상 포맷
        swapchaindesc.SampleDesc.Count = 1; // 멀티 샘플링 비활성화
        swapchaindesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 렌더 타겟으로 사용
        swapchaindesc.BufferCount = 2; // 더블 버퍼링
        swapchaindesc.OutputWindow = hWindow; // 렌더링할 창 핸들
        swapchaindesc.Windowed = TRUE; // 창 모드
        swapchaindesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 스왑 방식

        // Direct3D 장치와 스왑 체인을 생성
        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
            featurelevels, ARRAYSIZE(featurelevels), D3D11_SDK_VERSION,
            &swapchaindesc, &SwapChain, &Device, nullptr, &DeviceContext);

        // 생성된 스왑 체인의 정보 가져오기
        SwapChain->GetDesc(&swapchaindesc);

        // 뷰포트 정보 설정
        ViewportInfo = { 0.0f, 0.0f, (float)swapchaindesc.BufferDesc.Width, (float)swapchaindesc.BufferDesc.Height, 0.0f, 1.0f };
    }

    // Direct3D 장치 및 스왑 체인을 해제하는 함수
    void ReleaseDeviceAndSwapChain()
    {
        if (DeviceContext)
        {
            DeviceContext->Flush(); // 남아있는 GPU 명령 실행
        }

        if (SwapChain)
        {
            SwapChain->Release();
            SwapChain = nullptr;
        }

        if (Device)
        {
            Device->Release();
            Device = nullptr;
        }

        if (DeviceContext)
        {
            DeviceContext->Release();
            DeviceContext = nullptr;
        }
    }

    // 프레임 버퍼를 생성하는 함수
    void CreateFrameBuffer()
    {
        // 스왑 체인으로부터 백 버퍼 텍스처 가져오기
        SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&FrameBuffer);

        // 렌더 타겟 뷰 생성
        D3D11_RENDER_TARGET_VIEW_DESC framebufferRTVdesc = {};
        framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; // 색상 포맷
        framebufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; // 2D 텍스처

        Device->CreateRenderTargetView(FrameBuffer, &framebufferRTVdesc, &FrameBufferRTV);
    }

    // 프레임 버퍼를 해제하는 함수
    void ReleaseFrameBuffer()
    {
        if (FrameBuffer)
        {
            FrameBuffer->Release();
            FrameBuffer = nullptr;
        }

        if (FrameBufferRTV)
        {
            FrameBufferRTV->Release();
            FrameBufferRTV = nullptr;
        }
    }

    // 래스터라이저 상태를 생성하는 함수
    void CreateRasterizerState()
    {
        D3D11_RASTERIZER_DESC rasterizerdesc = {};
        rasterizerdesc.FillMode = D3D11_FILL_SOLID; // 채우기 모드
        rasterizerdesc.CullMode = D3D11_CULL_BACK; // 백 페이스 컬링

        Device->CreateRasterizerState(&rasterizerdesc, &RasterizerState);
    }

    // 래스터라이저 상태를 해제하는 함수
    void ReleaseRasterizerState()
    {
        if (RasterizerState)
        {
            RasterizerState->Release();
            RasterizerState = nullptr;
        }
    }

    // 렌더러에 사용된 모든 리소스를 해제하는 함수
    void Release()
    {
        RasterizerState->Release();

        // 렌더 타겟을 초기화
        DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

        ReleaseFrameBuffer();
        ReleaseDeviceAndSwapChain();
    }

    // 스왑 체인의 백 버퍼와 프론트 버퍼를 교체하여 화면에 출력
    void SwapBuffer()
    {
        SwapChain->Present(1, 0); // 1: VSync 활성화
    }

	// 셰이더 생성
    void CreateShader()
    {
        ID3DBlob* vertexshaderCSO;
        ID3DBlob* pixelshaderCSO;

        D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainVS", "vs_5_0", 0, 0, &vertexshaderCSO, nullptr);

        Device->CreateVertexShader(vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), nullptr, &SimpleVertexShader);

        D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainPS", "ps_5_0", 0, 0, &pixelshaderCSO, nullptr);

        Device->CreatePixelShader(pixelshaderCSO->GetBufferPointer(), pixelshaderCSO->GetBufferSize(), nullptr, &SimplePixelShader);

        D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        Device->CreateInputLayout(layout, ARRAYSIZE(layout), vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), &SimpleInputLayout);

        Stride = sizeof(FVertexSimple);

        vertexshaderCSO->Release();
        pixelshaderCSO->Release();
    }

	// 셰이더 해제
    void ReleaseShader()
    {
        if (SimpleInputLayout)
        {
            SimpleInputLayout->Release();
            SimpleInputLayout = nullptr;
        }

        if (SimplePixelShader)
        {
            SimplePixelShader->Release();
            SimplePixelShader = nullptr;
        }

        if (SimpleVertexShader)
        {
            SimpleVertexShader->Release();
            SimpleVertexShader = nullptr;
        }
    }

    void Prepare()
    {
        DeviceContext->ClearRenderTargetView(FrameBufferRTV, ClearColor);

        DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        DeviceContext->RSSetViewports(1, &ViewportInfo);
        DeviceContext->RSSetState(RasterizerState);

        DeviceContext->OMSetRenderTargets(1, &FrameBufferRTV, nullptr);
        DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    }

    void PrepareShader()
    {
        DeviceContext->VSSetShader(SimpleVertexShader, nullptr, 0);
        DeviceContext->PSSetShader(SimplePixelShader, nullptr, 0);
        DeviceContext->IASetInputLayout(SimpleInputLayout);
        // 버텍스 쉐이더에 상수 버퍼를 설정합니다.
        if (ConstantBuffer)
        {
            DeviceContext->VSSetConstantBuffers(0, 1, &ConstantBuffer);
        }
    }

    void RenderPrimitive(ID3D11Buffer* pBuffer, UINT numVertices)
    {
        UINT offset = 0;
        DeviceContext->IASetVertexBuffers(0, 1, &pBuffer, &Stride, &offset);

        DeviceContext->Draw(numVertices, 0);
    }

    ID3D11Buffer* CreateVertexBuffer(FVertexSimple* vertices, UINT byteWidth)
    {
        // 2. Create a vertex buffer
        D3D11_BUFFER_DESC vertexbufferdesc = {};
        vertexbufferdesc.ByteWidth = byteWidth;
        vertexbufferdesc.Usage = D3D11_USAGE_IMMUTABLE; // will never be updated 
        vertexbufferdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vertexbufferSRD = { vertices };

        ID3D11Buffer* vertexBuffer;

        Device->CreateBuffer(&vertexbufferdesc, &vertexbufferSRD, &vertexBuffer);

        return vertexBuffer;
    }

    void ReleaseVertexBuffer(ID3D11Buffer* vertexBuffer)
    {
        vertexBuffer->Release();
    }

    void CreateConstantBuffer()
    {
        D3D11_BUFFER_DESC constantbufferdesc = {};
        constantbufferdesc.ByteWidth = sizeof(FConstants) + 0xf & 0xfffffff0; // ensure constant buffer size is multiple of 16 bytes
        constantbufferdesc.Usage = D3D11_USAGE_DYNAMIC; // will be updated from CPU every frame
        constantbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        constantbufferdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        Device->CreateBuffer(&constantbufferdesc, nullptr, &ConstantBuffer);
    }

    void UpdateConstant(FVector offset, float scale = 1.0f) // 상수버퍼 업데이트
    {
        if (ConstantBuffer)
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            DeviceContext->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            FConstants* data = (FConstants*)mapped.pData;
            data->Offset = offset;
            data->Scale = scale;
            DeviceContext->Unmap(ConstantBuffer, 0);
        }
    }

    void ReleaseConstantBuffer()
    {
        if (ConstantBuffer)
        {
            ConstantBuffer->Release();
            ConstantBuffer = nullptr;
        }
    }

	void DrawSphere(const FVector& center, float scale) // 구 그리기
    {
        UpdateConstant(center, scale);
        UINT offset = 0;
        DeviceContext->IASetVertexBuffers(0, 1, &VertexBufferSphere, &Stride, &offset);
        DeviceContext->Draw(NumVerticesSphere, 0);
    }
};

class UPrimitive
{
public:
    virtual void Update(float t) = 0;
    virtual void Render(URenderer& renderer) = 0;
    virtual bool Collision(UPrimitive* other) = 0;
    virtual void Translate(const FVector& v) = 0;
};

class UBall : public UPrimitive
{
public:
    // 고정 변수
    FVector Location; // 현재 위치
	FVector Velocity; // 현재 속도
	float Radius; // 반지름
	float Mass; // 질량
	static int TotalNumBalls; // 생성된 공의 총 개수

    // 생성자: 질량 설정 및 개수 증가
    UBall(FVector loc, FVector vel, float radius)
        : Location(loc), Velocity(vel), Radius(radius)
    {
        // 질량은 반지름^2에 비례
        Mass = Radius * Radius;
        TotalNumBalls++;
    }

    // 소멸자: 개수 감소
    virtual ~UBall()
    {
        TotalNumBalls--;
    }

    // A: 물리 상태 업데이트
    void Update(float dt) override
    {
        // 중력 적용(Y축 아래 방향)
        if (EnableGravity)
        {
            Velocity.y += GravityAcceleration * dt;
        }

        // 위치 = 위치 + (속도 * 시간)
        Location += Velocity * dt;
		//화면 경계 충돌 처리
        const float left = -1.0f + Radius;
        const float right = 1.0f - Radius;
        const float top = -1.0f + Radius;
        const float bottom = 1.0f - Radius;

		// 경계에 닿으면 위치 보정 및 속도 반전 (에너지 손실 고려)
        if (Location.x <= left) { Location.x = left;   Velocity.x = -Velocity.x * 0.8f; }
        if (Location.x >= right) { Location.x = right;  Velocity.x = -Velocity.x * 0.8f; }
        if (Location.y <= top) { Location.y = top;    Velocity.y = -Velocity.y * 0.8f; }
        if (Location.y >= bottom) { Location.y = bottom; Velocity.y = -Velocity.y * 0.8f; }
    }

    // B: 렌더링 (Renderer의 상수 버퍼와 통신)
    void Render(URenderer& renderer) override
    {
        // 1. GPU에 이 공의 위치를 알림
        renderer.UpdateConstant(Location);
        // 2. 준비된 버텍스 버퍼를 그림
        renderer.DrawSphere(Location, Radius);
    }

    // C: 공 vs 공 충돌 판정 및 반응
    bool Collision(UPrimitive* other) override
    {
		// 다른 프리미티브가 공인지 확인
        UBall* otherBall = dynamic_cast<UBall*>(other);
        if (!otherBall || this == otherBall) return false;

		// 충돌 판정
        FVector toOther = otherBall->Location - Location;

		//거리 제곱과 반지름 합의 제곱 비교
        float distanceSq = toOther.SizeSquared();
        float sumRadius = Radius + otherBall->Radius;

		// 충돌하지 않음
        if (distanceSq > sumRadius * sumRadius || distanceSq < 1e-6f)
            return false;

		// 충돌함
        float distance = sqrtf(distanceSq);
        FVector normal = toOther.GetSafeNormal();

        // penetration 위치 보정
        float penetration = sumRadius - distance;
        if (penetration > 0.001f)
        {
            float totalMass = Mass + otherBall->Mass;

			// 각 공의 질량 비율에 따라 위치 보정
            float ratioA = otherBall->Mass / totalMass;
            float ratioB = Mass / totalMass;

            FVector correction = normal * penetration * 0.8f;
            Location -= correction * ratioA;
            otherBall->Location += correction * ratioB;
        }

        // impulse (튕김)
        FVector relativeVelocity = otherBall->Velocity - Velocity;
        float velAlongNormal = relativeVelocity.Dot(normal);

		// 충돌 후 튕김 처리
        if (velAlongNormal < -0.01f)
        {
			const float restitution = 0.6f; // 반발 계수 (0~1 사이 값)

            float j = -(1.0f + restitution) * velAlongNormal;
            j /= (1.0f / Mass + 1.0f / otherBall->Mass);

            FVector impulse = normal * j;
            Velocity -= impulse * (1.0f / Mass);
            otherBall->Velocity += impulse * (1.0f / otherBall->Mass);
        }

        return true;
    }

    //강제 위치 이동
    void Translate(const FVector& v) override
    {
        Location = v;
    }
};

// 정적 변수 초기화
int UBall::TotalNumBalls = 0;

UPrimitive** PrimitiveList = nullptr;
int CurrentBallCount = 0;
int DesiredBallCount = 0;

UBall* CreateRandomBall()
{
    FVector randomPos(
        -0.85f + (rand() % 1700) / 1000.0f,   // -0.85 ~ 0.85
        -0.85f + (rand() % 1700) / 1000.0f,
        0.0f
    );

    FVector randomVel(
        (rand() % 401 - 200) / 300.0f,    // -0.666 ~ +0.666 정도
        (rand() % 401 - 200) / 300.0f,
        0.0f
    );

    float randomRadius = 0.035f + (rand() % 31) / 100.0f;  // 0.025 ~ 0.055

    return new UBall(randomPos, randomVel, randomRadius);
}

void UpdateBallCount()
{
    if (DesiredBallCount == CurrentBallCount) return;

    if (DesiredBallCount > CurrentBallCount)
    {
        // 1. 임시 배열에 먼저 생성 (준비 완료 전까지 PrimitiveList를 건드리지 않음)
        UPrimitive** newList = new UPrimitive * [DesiredBallCount];

        // 기존 데이터 복사
        for (int i = 0; i < CurrentBallCount; i++)
            newList[i] = PrimitiveList[i];

        // 새 공 추가
        for (int i = CurrentBallCount; i < DesiredBallCount; i++)
            newList[i] = CreateRandomBall();

        // 2. 교체 작업
        UPrimitive** oldList = PrimitiveList;
        PrimitiveList = newList; // 여기서 포인터를 먼저 교체

        if (oldList != nullptr) delete[] oldList; // 이전 배열만 삭제

        CurrentBallCount = DesiredBallCount;
    }
    else if (DesiredBallCount < CurrentBallCount)
    {
        // 공 줄이기
        int toRemove = CurrentBallCount - DesiredBallCount;
        for (int k = 0; k < toRemove; k++)
        {
            if (CurrentBallCount <= 0) break;
            int removeIdx = rand() % CurrentBallCount;

            delete PrimitiveList[removeIdx];
            PrimitiveList[removeIdx] = PrimitiveList[CurrentBallCount - 1];
            PrimitiveList[CurrentBallCount - 1] = nullptr;
            CurrentBallCount--;
        }
    }
}


extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 각종 메시지를 처리할 함수
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return true;
	}
	switch (message)
	{
	case WM_DESTROY:
		// Signal that the app should quit
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    // 윈도우 클래스 이름
    WCHAR WindowClass[] = L"JungleWindowClass";

    // 윈도우 타이틀바에 표시될 이름
    WCHAR Title[] = L"Game Tech Lab";

    // 각종 메시지를 처리할 함수인 WndProc의 함수 포인터를 WindowClass 구조체에 넣는다.
    WNDCLASSW wndclass = { 0, WndProc, 0, 0, 0, 0, 0, 0, 0, WindowClass };

    // 윈도우 클래스 등록
    RegisterClassW(&wndclass);

    // 1024 x 1024 크기에 윈도우 생성
    HWND hWnd = CreateWindowExW(0, WindowClass, Title, WS_POPUP | WS_VISIBLE | WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 1024,
        nullptr, nullptr, hInstance, nullptr);

    bool bIsExit = false;


    // Renderer Class를 생성합니다.
    URenderer	renderer;

    // D3D11 생성하는 함수를 호출합니다.
    renderer.Create(hWnd);
    renderer.CreateShader();

    // 여기에 생성 함수를 추가합니다.	
    renderer.CreateConstantBuffer();

	// ImGui 초기화
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init((void*)hWnd);
    ImGui_ImplDX11_Init(renderer.Device, renderer.DeviceContext);

    UINT numVerticesSphere = sizeof(sphere_vertices) / sizeof(FVertexSimple);


    renderer.VertexBufferSphere = renderer.CreateVertexBuffer(
        sphere_vertices,
        sizeof(sphere_vertices)
    );

    renderer.NumVerticesSphere = numVerticesSphere;

    // 도형의 움직임 정도를 담을 offset 변수를 Main 루프 바로 앞에 정의 하세요.	
    FVector	offset(0.0f); // 키보드 입력에 따른 속도 
	FVector	velocity(0.0f); // 속도

    // FPS 제한을 위한 설정
    const int targetFPS = 30;
    const double targetFrameTime = 1000.0 / targetFPS; // 한 프레임의 목표 시간 (밀리초 단위)

    // 고성능 타이머 초기화
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);

    LARGE_INTEGER startTime, endTime;
    double elapsedTime = 0.0;

    while (bIsExit == false)
    {
        // Main Loop (Quit Message가 들어오기 전까지 아래 Loop를 무한히 실행하게 됨)
        while (bIsExit == false)
        {
            // 루프 시작 시간 기록
            QueryPerformanceCounter(&startTime);

            MSG msg;

            // 처리할 메시지가 더 이상 없을때 까지 수행
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                // 키 입력 메시지를 번역
                TranslateMessage(&msg);

                // 메시지를 적절한 윈도우 프로시저에 전달, 메시지가 위에서 등록한 WndProc 으로 전달됨
                DispatchMessage(&msg);

                if (msg.message == WM_QUIT)
                {
                    bIsExit = true;
                    break;
                }
            }
            ////////////////////////////////////////////
            // 매번 실행되는 코드를 여기에 추가합니다.

			//1. 공 개수 업데이트
            UpdateBallCount();

			//2. delta time 계산
            double dt = elapsedTime / 1000.0;

			//3. 물리 업데이트
             for (int i = 0; i < CurrentBallCount; i++)
             {
                 PrimitiveList[i]->Update(dt);
             }
			 //4. 충돌 처리
             const int collisionPasses = 2;
             for (int pass = 0; pass < collisionPasses; pass++)
             {
                 for (int i = 0; i < CurrentBallCount; i++)
                 {
                     for (int j = i + 1; j < CurrentBallCount; j++)
                     {
                         if (PrimitiveList[i] && PrimitiveList[j])
                         {
                             PrimitiveList[i]->Collision(PrimitiveList[j]);
                         }
                     }
                 }
             }
			 // 5. 렌더링
             renderer.Prepare();       // 화면 지우기
             renderer.PrepareShader(); // 쉐이더 장착

             // 모든 공 그리기
             for (int i = 0; i < CurrentBallCount; i++)
                 PrimitiveList[i]->Render(renderer);
            // offset을 상수 버퍼로 업데이트 합니다.
            renderer.UpdateConstant(offset);

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // 이후 ImGui UI 컨트롤 추가는 ImGui::NewFrame()과 ImGui::Render() 사이인 여기에 위치합니다.
            ImGui::Begin("Jungle Property Window");
            ImGui::Text("Hello Jungle World!");
            if (ImGui::Button("Quit this app"))
            {
                // 현재 윈도우에 Quit 메시지를 메시지 큐로 보냄
                PostMessage(hWnd, WM_QUIT, 0, 0);
            }
            // Hello Jungle World 아래에 CheckBox와 bBoundBallToScreen 변수를 연결합니다.
            ImGui::InputInt("Number of Balls", &DesiredBallCount);
			ImGui::Checkbox("Gravity", &EnableGravity);
            ImGui::End();

            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            // 다 그렸으면 버퍼를 교환
            renderer.SwapBuffer();
            // 여기에 추가합니다.		
            do
            {
                Sleep(0);

                // 루프 종료 시간 기록
                QueryPerformanceCounter(&endTime);

                // 한 프레임이 소요된 시간 계산 (밀리초 단위로 변환)
                elapsedTime = (endTime.QuadPart - startTime.QuadPart) * 1000.0 / frequency.QuadPart;

            } while (elapsedTime < targetFrameTime);
            ////////////////////////////////////////////
        }

        // 소멸하는 코드를 여기에 추가합니다.
        // 렌더러 소멸 직전에 쉐이더를 소멸 시키는 함수를 호출합니다.
		if (PrimitiveList) // 원 소멸
        {
            for (int i = 0; i < CurrentBallCount; i++)
            {
                delete PrimitiveList[i];
                PrimitiveList[i] = nullptr;
            }
            delete[] PrimitiveList;
            PrimitiveList = nullptr;
        }
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        renderer.ReleaseVertexBuffer(renderer.VertexBufferSphere);
        renderer.ReleaseConstantBuffer();
        renderer.ReleaseShader();
        renderer.Release();
        return 0;
    }
}
