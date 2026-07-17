#pragma once

#include "Config.h"
#include "DisplayModule.h"

class FLogger;

class FSpaceImpact
{
public:
    explicit FSpaceImpact(FLogger& InLogger);
    ~FSpaceImpact();

    void Start();
    void Update();
    void HandleButton(EButtonEvent Button);
    void Render(FDisplayModule& Display);
    bool IsActive() const;
    bool IsGameOver() const;

private:
    struct FStar
    {
        bool bActive = false;
        float X = 0.0f;
        float Y = 0.0f;
        float Speed = 0.0f;
    };

    struct FProjectile
    {
        bool bActive = false;
        bool bFriendly = true;
        float X = 0.0f;
        float Y = 0.0f;
        float VX = 0.0f;
        float VY = 0.0f;
        uint16_t Radius = 1;
    };

    struct FAsteroid
    {
        bool bActive = false;
        float X = 0.0f;
        float Y = 0.0f;
        float VX = 0.0f;
        uint8_t Radius = 3;
    };

    struct FEnemy
    {
        bool bActive = false;
        float X = 0.0f;
        float Y = 0.0f;
        float VX = 0.0f;
        uint8_t HitPoints = 2;
        uint32_t NextShotAtMs = 0;
    };

    struct FUpgrade
    {
        bool bActive = false;
        float X = 0.0f;
        float Y = 0.0f;
        float VX = 0.0f;
        uint8_t LevelGain = 1;
    };

    void ResetStars();
    void ResetEntities();
    void SpawnStar(uint8_t Index, bool bRandomX);
    void SpawnAsteroid();
    void SpawnEnemy();
    void SpawnUpgrade();
    void SpawnProjectile(bool bFriendly, float X, float Y, float VX, float VY, uint16_t Radius = 1);
    void SpawnPlayerVolley();
    void SpawnEnemyShot(const FEnemy& Enemy);

    void UpdateStars(float DeltaSeconds);
    void UpdateProjectiles(float DeltaSeconds);
    void UpdateAsteroids(float DeltaSeconds);
    void UpdateEnemies(float DeltaSeconds);
    void UpdateUpgrades(float DeltaSeconds);
    void HandleCollisions();
    void AddScore(uint16_t Delta);

private:
    static constexpr uint8_t MaxStars = 14;
    static constexpr uint8_t MaxProjectiles = 40;
    static constexpr uint8_t MaxAsteroids = 8;
    static constexpr uint8_t MaxEnemies = 5;
    static constexpr uint8_t MaxUpgrades = 3;
    static constexpr int16_t HudHeight = 10;
    static constexpr int16_t FieldWidth = 128;
    static constexpr int16_t FieldHeight = 64;
    static constexpr int16_t ShipX = 12;
    static constexpr int16_t ShipWidth = 8;
    static constexpr int16_t ShipHeight = 6;

    FLogger& Logger;

    bool bActive = false;
    bool bGameOver = false;

    uint32_t LastUpdateMs = 0;
    uint32_t LastFireAtMs = 0;
    uint32_t NextAsteroidAtMs = 0;
    uint32_t NextEnemyAtMs = 0;
    uint32_t NextUpgradeAtMs = 0;

    int16_t ShipY = 32;
    uint8_t HitPoints = 5;
    uint8_t WeaponLevel = 1;
    uint32_t Score = 0;

    FStar Stars[MaxStars];
    FProjectile Projectiles[MaxProjectiles];
    FAsteroid Asteroids[MaxAsteroids];
    FEnemy Enemies[MaxEnemies];
    FUpgrade Upgrades[MaxUpgrades];
};
