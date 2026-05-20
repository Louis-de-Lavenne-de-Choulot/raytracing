#pragma once
namespace HonHengine {class GameObject; class IScript { public: virtual ~IScript() = default; virtual void Start() = 0; virtual void Update(float deltaTime) = 0; virtual void OnDestroy() = 0; virtual void OnReload() {} GameObject* gameObject = nullptr; }; }
