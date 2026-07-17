#include "SpaceImpact.h"

#include "Logger.h"

#include <U8g2lib.h>
#include <math.h>

FSpaceImpact::FSpaceImpact(FLogger& InLogger)
    : Logger(InLogger)
{
}

FSpaceImpact::~FSpaceImpact() = default;

void FSpaceImpact::Start()
{
    bActive = true;
    bGameOver = false;
    LastUpdateMs = millis();
    LastFireAtMs = 0;
    NextAsteroidAtMs = LastUpdateMs + 500;
    NextEnemyAtMs = LastUpdateMs + 1800;
    NextUpgradeAtMs = LastUpdateMs + 6000;
    ShipY = HudHeight + ((FieldHeight - HudHeight) / 2);
    HitPoints = 5;
    WeaponLevel = 1;
    Score = 0;
    ResetEntities();
    ResetStars();
    Logger.Log(ELogLevel::Info, "Space Impact started.");
}

bool FSpaceImpact::IsActive() const
{
    return bActive;
}

bool FSpaceImpact::IsGameOver() const
{
    return bGameOver;
}

void FSpaceImpact::ResetEntities()
{
    for (uint8_t Index = 0; Index < MaxProjectiles; ++Index)
    {
        Projectiles[Index] = FProjectile{};
    }
    for (uint8_t Index = 0; Index < MaxAsteroids; ++Index)
    {
        Asteroids[Index] = FAsteroid{};
    }
    for (uint8_t Index = 0; Index < MaxEnemies; ++Index)
    {
        Enemies[Index] = FEnemy{};
    }
    for (uint8_t Index = 0; Index < MaxUpgrades; ++Index)
    {
        Upgrades[Index] = FUpgrade{};
    }
}

void FSpaceImpact::ResetStars()
{
    for (uint8_t Index = 0; Index < MaxStars; ++Index)
    {
        SpawnStar(Index, true);
    }
}

void FSpaceImpact::SpawnStar(uint8_t Index, bool bRandomX)
{
    Stars[Index].bActive = true;
    Stars[Index].X = bRandomX ? static_cast<float>(random(0, FieldWidth)) : static_cast<float>(FieldWidth - 1);
    Stars[Index].Y = static_cast<float>(random(HudHeight + 1, FieldHeight - 1));
    Stars[Index].Speed = static_cast<float>(12 + random(0, 28));
}

void FSpaceImpact::HandleButton(EButtonEvent Button)
{
    if (!bActive)
    {
        return;
    }

    if (bGameOver)
    {
        if (Button == EButtonEvent::Ok)
        {
            Start();
        }
        return;
    }

    if (Button == EButtonEvent::Up)
    {
        ShipY -= 4;
        if (ShipY < HudHeight + 4)
        {
            ShipY = HudHeight + 4;
        }
    }
    else if (Button == EButtonEvent::Down)
    {
        ShipY += 4;
        if (ShipY > FieldHeight - 4)
        {
            ShipY = FieldHeight - 4;
        }
    }
    else if (Button == EButtonEvent::Ok)
    {
        if ((millis() - LastFireAtMs) >= 80)
        {
            SpawnPlayerVolley();
            LastFireAtMs = millis();
        }
    }
}

void FSpaceImpact::Update()
{
    if (!bActive)
    {
        return;
    }

    const uint32_t NowMs = millis();
    if (LastUpdateMs == 0)
    {
        LastUpdateMs = NowMs;
    }

    uint32_t DeltaMs = NowMs - LastUpdateMs;
    if (DeltaMs > 40)
    {
        DeltaMs = 40;
    }
    LastUpdateMs = NowMs;

    const float DeltaSeconds = static_cast<float>(DeltaMs) / 1000.0f;

    UpdateStars(DeltaSeconds);

    if (bGameOver)
    {
        return;
    }

    if ((NowMs - LastFireAtMs) >= 280)
    {
        SpawnPlayerVolley();
        LastFireAtMs = NowMs;
    }

    if (NowMs >= NextAsteroidAtMs)
    {
        SpawnAsteroid();
        NextAsteroidAtMs = NowMs + static_cast<uint32_t>(600 + random(0, 700));
    }

    if (NowMs >= NextEnemyAtMs)
    {
        SpawnEnemy();
        NextEnemyAtMs = NowMs + static_cast<uint32_t>(2000 + random(0, 2200));
    }

    if (NowMs >= NextUpgradeAtMs)
    {
        SpawnUpgrade();
        NextUpgradeAtMs = NowMs + static_cast<uint32_t>(6500 + random(0, 4500));
    }

    UpdateProjectiles(DeltaSeconds);
    UpdateAsteroids(DeltaSeconds);
    UpdateEnemies(DeltaSeconds);
    UpdateUpgrades(DeltaSeconds);
    HandleCollisions();

    if (HitPoints == 0)
    {
        bGameOver = true;
        Logger.Log(ELogLevel::Info, "Space Impact game over.");
    }
}

void FSpaceImpact::SpawnAsteroid()
{
    for (uint8_t Index = 0; Index < MaxAsteroids; ++Index)
    {
        if (!Asteroids[Index].bActive)
        {
            Asteroids[Index].bActive = true;
            Asteroids[Index].X = static_cast<float>(FieldWidth + random(0, 12));
            Asteroids[Index].Y = static_cast<float>(random(HudHeight + 4, FieldHeight - 4));
            Asteroids[Index].VX = static_cast<float>(20 + random(0, 26));
            Asteroids[Index].Radius = static_cast<uint8_t>(2 + random(0, 4));
            return;
        }
    }
}

void FSpaceImpact::SpawnEnemy()
{
    for (uint8_t Index = 0; Index < MaxEnemies; ++Index)
    {
        if (!Enemies[Index].bActive)
        {
            Enemies[Index].bActive = true;
            Enemies[Index].X = static_cast<float>(FieldWidth + random(0, 10));
            Enemies[Index].Y = static_cast<float>(random(HudHeight + 6, FieldHeight - 6));
            Enemies[Index].VX = static_cast<float>(18 + random(0, 20));
            Enemies[Index].HitPoints = static_cast<uint8_t>(2 + random(0, 2));
            Enemies[Index].NextShotAtMs = millis() + static_cast<uint32_t>(900 + random(0, 1100));
            return;
        }
    }
}

void FSpaceImpact::SpawnUpgrade()
{
    for (uint8_t Index = 0; Index < MaxUpgrades; ++Index)
    {
        if (!Upgrades[Index].bActive)
        {
            Upgrades[Index].bActive = true;
            Upgrades[Index].X = static_cast<float>(FieldWidth + random(0, 16));
            Upgrades[Index].Y = static_cast<float>(random(HudHeight + 6, FieldHeight - 6));
            Upgrades[Index].VX = static_cast<float>(18 + random(0, 14));
            Upgrades[Index].LevelGain = 1;
            return;
        }
    }
}

void FSpaceImpact::SpawnProjectile(bool bFriendly, float X, float Y, float VX, float VY, uint16_t Radius)
{
    for (uint8_t Index = 0; Index < MaxProjectiles; ++Index)
    {
        if (!Projectiles[Index].bActive)
        {
            Projectiles[Index].bActive = true;
            Projectiles[Index].bFriendly = bFriendly;
            Projectiles[Index].X = X;
            Projectiles[Index].Y = Y;
            Projectiles[Index].VX = VX;
            Projectiles[Index].VY = VY;
            Projectiles[Index].Radius = Radius;
            return;
        }
    }
}

void FSpaceImpact::SpawnPlayerVolley()
{
    const uint8_t ProjectileCount = (WeaponLevel > 10) ? 10 : WeaponLevel;
    const float CenterY = static_cast<float>(ShipY);
    const float BaseX = static_cast<float>(ShipX + ShipWidth);
    if (ProjectileCount == 0)
    {
        return;
    }

    const float Step = 8.0f;
    const float Middle = (static_cast<float>(ProjectileCount) - 1.0f) * 0.5f;
    for (uint8_t Index = 0; Index < ProjectileCount; ++Index)
    {
        const float Offset = (static_cast<float>(Index) - Middle) * Step;
        const float VX = 86.0f;
        const float VY = Offset * 0.55f;
        SpawnProjectile(true, BaseX, CenterY, VX, VY, 1);
    }
}

void FSpaceImpact::SpawnEnemyShot(const FEnemy& Enemy)
{
    const float SourceX = Enemy.X - 4.0f;
    const float SourceY = Enemy.Y;
    const float TargetX = static_cast<float>(ShipX);
    const float TargetY = static_cast<float>(ShipY);
    float DX = TargetX - SourceX;
    float DY = TargetY - SourceY;
    const float Length = sqrtf((DX * DX) + (DY * DY));
    if (Length > 0.001f)
    {
        DX /= Length;
        DY /= Length;
    }
    SpawnProjectile(false, SourceX, SourceY, DX * 52.0f, DY * 52.0f, 1);
}

void FSpaceImpact::UpdateStars(float DeltaSeconds)
{
    for (uint8_t Index = 0; Index < MaxStars; ++Index)
    {
        if (!Stars[Index].bActive)
        {
            SpawnStar(Index, true);
            continue;
        }

        Stars[Index].X -= Stars[Index].Speed * DeltaSeconds;
        if (Stars[Index].X < 0.0f)
        {
            SpawnStar(Index, false);
        }
    }
}

void FSpaceImpact::UpdateProjectiles(float DeltaSeconds)
{
    for (uint8_t Index = 0; Index < MaxProjectiles; ++Index)
    {
        if (!Projectiles[Index].bActive)
        {
            continue;
        }

        Projectiles[Index].X += Projectiles[Index].VX * DeltaSeconds;
        Projectiles[Index].Y += Projectiles[Index].VY * DeltaSeconds;
        if (Projectiles[Index].X < -4.0f || Projectiles[Index].X > static_cast<float>(FieldWidth + 4) || Projectiles[Index].Y < static_cast<float>(HudHeight) || Projectiles[Index].Y > static_cast<float>(FieldHeight))
        {
            Projectiles[Index].bActive = false;
        }
    }
}

void FSpaceImpact::UpdateAsteroids(float DeltaSeconds)
{
    for (uint8_t Index = 0; Index < MaxAsteroids; ++Index)
    {
        if (!Asteroids[Index].bActive)
        {
            continue;
        }

        Asteroids[Index].X -= Asteroids[Index].VX * DeltaSeconds;
        if (Asteroids[Index].X < -8.0f)
        {
            Asteroids[Index].bActive = false;
        }
    }
}

void FSpaceImpact::UpdateEnemies(float DeltaSeconds)
{
    const uint32_t NowMs = millis();
    for (uint8_t Index = 0; Index < MaxEnemies; ++Index)
    {
        if (!Enemies[Index].bActive)
        {
            continue;
        }

        Enemies[Index].X -= Enemies[Index].VX * DeltaSeconds;
        if (Enemies[Index].X < -10.0f)
        {
            Enemies[Index].bActive = false;
            continue;
        }

        if (NowMs >= Enemies[Index].NextShotAtMs)
        {
            SpawnEnemyShot(Enemies[Index]);
            Enemies[Index].NextShotAtMs = NowMs + static_cast<uint32_t>(1300 + random(0, 1400));
        }
    }
}

void FSpaceImpact::UpdateUpgrades(float DeltaSeconds)
{
    for (uint8_t Index = 0; Index < MaxUpgrades; ++Index)
    {
        if (!Upgrades[Index].bActive)
        {
            continue;
        }

        Upgrades[Index].X -= Upgrades[Index].VX * DeltaSeconds;
        if (Upgrades[Index].X < -8.0f)
        {
            Upgrades[Index].bActive = false;
        }
    }
}

void FSpaceImpact::HandleCollisions()
{
    const float ShipLeft = static_cast<float>(ShipX);
    const float ShipRight = static_cast<float>(ShipX + ShipWidth);
    const float ShipTop = static_cast<float>(ShipY - ShipHeight);
    const float ShipBottom = static_cast<float>(ShipY + ShipHeight);

    for (uint8_t ProjectileIndex = 0; ProjectileIndex < MaxProjectiles; ++ProjectileIndex)
    {
        if (!Projectiles[ProjectileIndex].bActive)
        {
            continue;
        }

        if (Projectiles[ProjectileIndex].bFriendly)
        {
            for (uint8_t AsteroidIndex = 0; AsteroidIndex < MaxAsteroids; ++AsteroidIndex)
            {
                if (!Asteroids[AsteroidIndex].bActive)
                {
                    continue;
                }

                const float DX = Projectiles[ProjectileIndex].X - Asteroids[AsteroidIndex].X;
                const float DY = Projectiles[ProjectileIndex].Y - Asteroids[AsteroidIndex].Y;
                const float Radius = static_cast<float>(Projectiles[ProjectileIndex].Radius + Asteroids[AsteroidIndex].Radius);
                if ((DX * DX) + (DY * DY) <= (Radius * Radius))
                {
                    Projectiles[ProjectileIndex].bActive = false;
                    Asteroids[AsteroidIndex].bActive = false;
                    AddScore(5);
                    break;
                }
            }

            if (!Projectiles[ProjectileIndex].bActive)
            {
                continue;
            }

            for (uint8_t EnemyIndex = 0; EnemyIndex < MaxEnemies; ++EnemyIndex)
            {
                if (!Enemies[EnemyIndex].bActive)
                {
                    continue;
                }

                const float DX = Projectiles[ProjectileIndex].X - Enemies[EnemyIndex].X;
                const float DY = Projectiles[ProjectileIndex].Y - Enemies[EnemyIndex].Y;
                if ((DX * DX) + (DY * DY) <= 16.0f)
                {
                    Projectiles[ProjectileIndex].bActive = false;
                    if (Enemies[EnemyIndex].HitPoints > 0)
                    {
                        --Enemies[EnemyIndex].HitPoints;
                    }
                    if (Enemies[EnemyIndex].HitPoints == 0)
                    {
                        Enemies[EnemyIndex].bActive = false;
                        AddScore(12);
                    }
                    break;
                }
            }
        }
        else
        {
            const float X = Projectiles[ProjectileIndex].X;
            const float Y = Projectiles[ProjectileIndex].Y;
            if (X >= ShipLeft && X <= ShipRight && Y >= ShipTop && Y <= ShipBottom)
            {
                Projectiles[ProjectileIndex].bActive = false;
                if (HitPoints > 0)
                {
                    --HitPoints;
                }
            }
        }
    }

    for (uint8_t AsteroidIndex = 0; AsteroidIndex < MaxAsteroids; ++AsteroidIndex)
    {
        if (!Asteroids[AsteroidIndex].bActive)
        {
            continue;
        }

        const float Left = Asteroids[AsteroidIndex].X - Asteroids[AsteroidIndex].Radius;
        const float Right = Asteroids[AsteroidIndex].X + Asteroids[AsteroidIndex].Radius;
        const float Top = Asteroids[AsteroidIndex].Y - Asteroids[AsteroidIndex].Radius;
        const float Bottom = Asteroids[AsteroidIndex].Y + Asteroids[AsteroidIndex].Radius;
        if (Right >= ShipLeft && Left <= ShipRight && Bottom >= ShipTop && Top <= ShipBottom)
        {
            Asteroids[AsteroidIndex].bActive = false;
            if (HitPoints > 0)
            {
                --HitPoints;
            }
        }
    }

    for (uint8_t EnemyIndex = 0; EnemyIndex < MaxEnemies; ++EnemyIndex)
    {
        if (!Enemies[EnemyIndex].bActive)
        {
            continue;
        }

        const float Left = Enemies[EnemyIndex].X - 4.0f;
        const float Right = Enemies[EnemyIndex].X + 4.0f;
        const float Top = Enemies[EnemyIndex].Y - 3.0f;
        const float Bottom = Enemies[EnemyIndex].Y + 3.0f;
        if (Right >= ShipLeft && Left <= ShipRight && Bottom >= ShipTop && Top <= ShipBottom)
        {
            Enemies[EnemyIndex].bActive = false;
            if (HitPoints > 0)
            {
                --HitPoints;
            }
        }
    }

    for (uint8_t UpgradeIndex = 0; UpgradeIndex < MaxUpgrades; ++UpgradeIndex)
    {
        if (!Upgrades[UpgradeIndex].bActive)
        {
            continue;
        }

        const float Left = Upgrades[UpgradeIndex].X - 3.0f;
        const float Right = Upgrades[UpgradeIndex].X + 3.0f;
        const float Top = Upgrades[UpgradeIndex].Y - 3.0f;
        const float Bottom = Upgrades[UpgradeIndex].Y + 3.0f;
        if (Right >= ShipLeft && Left <= ShipRight && Bottom >= ShipTop && Top <= ShipBottom)
        {
            Upgrades[UpgradeIndex].bActive = false;
            if (WeaponLevel < 10)
            {
                ++WeaponLevel;
            }
            AddScore(20);
        }
    }
}

void FSpaceImpact::AddScore(uint16_t Delta)
{
    Score += Delta;
}

void FSpaceImpact::Render(FDisplayModule& DisplayModule)
{
    if (!bActive)
    {
        return;
    }

    if (!DisplayModule.BeginCustomFrame())
    {
        return;
    }

    U8G2_SSD1306_128X64_NONAME_F_HW_I2C& Display = DisplayModule.GetNativeDisplay();
    Display.setDrawColor(1);
    Display.setFont(u8g2_font_5x7_tf);

    Display.drawStr(0, 7, ("HP:" + String(HitPoints)).c_str());
    Display.drawStr(30, 7, ("W:" + String(WeaponLevel)).c_str());
    Display.drawStr(58, 7, ("S:" + String(Score)).c_str());
    Display.drawHLine(0, HudHeight - 1, FieldWidth);

    for (uint8_t Index = 0; Index < MaxStars; ++Index)
    {
        if (Stars[Index].bActive)
        {
            Display.drawPixel(static_cast<int16_t>(Stars[Index].X), static_cast<int16_t>(Stars[Index].Y));
        }
    }

    if (!bGameOver)
    {
        Display.drawLine(ShipX - 3, ShipY, ShipX + 4, ShipY - 4);
        Display.drawLine(ShipX - 3, ShipY, ShipX + 4, ShipY + 4);
        Display.drawLine(ShipX + 4, ShipY - 4, ShipX + 4, ShipY + 4);
        Display.drawPixel(ShipX + 6, ShipY);

        for (uint8_t Index = 0; Index < MaxProjectiles; ++Index)
        {
            if (!Projectiles[Index].bActive)
            {
                continue;
            }

            const int16_t X = static_cast<int16_t>(Projectiles[Index].X);
            const int16_t Y = static_cast<int16_t>(Projectiles[Index].Y);
            if (Projectiles[Index].bFriendly)
            {
                Display.drawPixel(X, Y);
                Display.drawPixel(X + 1, Y);
            }
            else
            {
                Display.drawBox(X - 1, Y - 1, 2, 2);
            }
        }

        for (uint8_t Index = 0; Index < MaxAsteroids; ++Index)
        {
            if (Asteroids[Index].bActive)
            {
                Display.drawDisc(static_cast<int16_t>(Asteroids[Index].X), static_cast<int16_t>(Asteroids[Index].Y), Asteroids[Index].Radius, U8G2_DRAW_ALL);
            }
        }

        for (uint8_t Index = 0; Index < MaxEnemies; ++Index)
        {
            if (!Enemies[Index].bActive)
            {
                continue;
            }

            const int16_t X = static_cast<int16_t>(Enemies[Index].X);
            const int16_t Y = static_cast<int16_t>(Enemies[Index].Y);
            Display.drawFrame(X - 4, Y - 3, 8, 6);
            Display.drawPixel(X - 5, Y);
            Display.drawPixel(X + 5, Y);
        }

        for (uint8_t Index = 0; Index < MaxUpgrades; ++Index)
        {
            if (!Upgrades[Index].bActive)
            {
                continue;
            }

            const int16_t X = static_cast<int16_t>(Upgrades[Index].X);
            const int16_t Y = static_cast<int16_t>(Upgrades[Index].Y);
            Display.drawFrame(X - 3, Y - 3, 6, 6);
            Display.drawLine(X - 2, Y, X + 2, Y);
            Display.drawLine(X, Y - 2, X, Y + 2);
        }
    }
    else
    {
        Display.drawStr(28, 26, "SPACE IMPACT");
        Display.drawStr(34, 36, "GAME OVER");
        Display.drawStr(14, 48, "OK=restart BACK=menu");
    }

    DisplayModule.EndCustomFrame();
}
