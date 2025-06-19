#include <GL/glut.h>
#include <GL/glu.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

const int SCREEN_WIDTH = 800, SCREEN_HEIGHT = 600;
const float PLAYER_SPEED = 8.0f, LASER_SPEED = 15.0f, SHOOT_COOLDOWN = 400.0f;
const float GRAVITY = 0.5f, BALL_BOUNCE_FACTOR = 1.0f;
const float INVISIBILITY_DURATION = 10000.0f, METEOR_DROP_INTERVAL = 10000.0f;
const float METEOR_FALL_SPEED = 3.0f, METEOR_EXPLOSION_RADIUS = 100.0f;
const int MAX_INVISIBILITY_USES = 3, INVISIBILITY_REWARD_INTERVAL = 1000;
const int BALL_SPAWN_SCORE_INTERVAL = 500;
const float BG_COLOR[3] = { 0.05f, 0.15f, 0.3f }, GROUND_COLOR[3] = { 0.3f, 0.5f, 0.2f };
const float UI_PANEL_COLOR[4] = { 0.1f, 0.1f, 0.15f, 0.7f }, UI_HIGHLIGHT_COLOR[3] = { 0.4f, 0.8f, 1.0f };
const float UI_TEXT_COLOR[3] = { 1.0f, 1.0f, 1.0f };
const int BUBBLE_EFFECT_BOUNCE = 0, BUBBLE_EFFECT_POP = 1, BUBBLE_EFFECT_SPLIT = 2;

struct Vector2 { float x, y; };
struct Ball { Vector2 position, velocity; float radius; int points; };
struct Laser { Vector2 position; float startX; bool isActive; };
struct Player {
    Vector2 position; bool isMoving; float width, height; int lastShootTime;
    bool isInvisible; int invisibilityStartTime; int remainingInvisibilityUses;
    float direction;
};
struct Meteor { Vector2 position; bool isActive, hasExploded; int explosionStartTime; };
struct BubbleEffect { Vector2 position; float radius; int type; int startTime; float alpha; };

Player player;
std::vector<Ball> balls;
std::vector<BubbleEffect> bubbleEffects;
std::vector<Meteor> meteors;
Laser laser = { {0, 0}, 0, false };
bool gameOver = false, leftPressed = false, rightPressed = false, spacePressed = false, invisibilityPressed = false;
int score = 0, highScore = 0, gameStartTime = 0, lastScoreIncrementTime = 0;
int lastInvisibilityRewardScore = 0, lastBallSpawnScore = 0, lastMeteorDropTime = 0;
GLuint backgroundTexture, ufoTexture, meteorTexture;

int GetTime() { return glutGet(GLUT_ELAPSED_TIME); }

void LoadHighScore() {
#ifdef _MSC_VER
    FILE* file = nullptr;
    errno_t err = fopen_s(&file, "highscore.txt", "r");
    if (err != 0 || !file) return;
    int result = fscanf_s(file, "%d", &highScore);
    fclose(file);
    if (result != 1) highScore = 0;
#else
    FILE* file = fopen("highscore.txt", "r");
    if (file) {
        int result = fscanf(file, "%d", &highScore);
        fclose(file);
        if (result != 1) highScore = 0;
    }
#endif
}

void SaveHighScore() {
    if (score <= highScore) return;
    highScore = score;
#ifdef _MSC_VER
    FILE* file = nullptr;
    errno_t err = fopen_s(&file, "highscore.txt", "w");
    if (err != 0 || !file) return;
    fprintf(file, "%d", highScore);
    fclose(file);
#else
    FILE* file = fopen("highscore.txt", "w");
    if (file) {
        fprintf(file, "%d", highScore);
        fclose(file);
    }
#endif
}

void SpawnBall(float x, float y, float radius) {
    Ball ball;
    ball.position = { x, y };
    ball.radius = radius;
    float speed = 1.0f + static_cast<float>(rand()) / RAND_MAX * 0.3f;
    float direction = (rand() % 2 ? 1.0f : -1.0f);
    ball.velocity = { direction * (2.0f + radius / 15.0f) * speed, -1.5f };
    ball.points = static_cast<int>(100.0f / (radius / 10.0f));
    balls.push_back(ball);
}

void SpawnRandomBall() {
    float x = 100.0f + static_cast<float>(rand() % (SCREEN_WIDTH - 200));
    float radius = 20.0f + static_cast<float>(rand() % 41);
    SpawnBall(x, 100, radius);
    lastBallSpawnScore = score;
}

void CheckBallSpawn() {
    int spawnIntervals = score / BALL_SPAWN_SCORE_INTERVAL;
    int lastSpawnIntervals = lastBallSpawnScore / BALL_SPAWN_SCORE_INTERVAL;
    if (spawnIntervals > lastSpawnIntervals || (balls.empty() && score > lastBallSpawnScore)) {
        SpawnRandomBall();
    }
}

void SplitBall(int index) {
    BubbleEffect effect = { balls[index].position, balls[index].radius, BUBBLE_EFFECT_SPLIT, GetTime(), 1.0f };
    bubbleEffects.push_back(effect);
    score += balls[index].points;

    if (balls[index].radius > 20) {
        float newRadius = balls[index].radius / 2;
        Ball newBall1 = balls[index];
        newBall1.radius = newRadius;
        newBall1.velocity = { balls[index].velocity.x + 1.5f, -balls[index].velocity.y };
        newBall1.points = balls[index].points * 2;
        Ball newBall2 = balls[index];
        newBall2.radius = newRadius;
        newBall2.velocity = { -balls[index].velocity.x - 1.5f, -balls[index].velocity.y };
        newBall2.points = balls[index].points * 2;
        balls.push_back(newBall1);
        balls.push_back(newBall2);
    }

    balls.erase(balls.begin() + index);
    CheckBallSpawn();
}

bool CheckLaserCollision(const Ball& ball) {
    if (!laser.isActive) return false;
    float dx = fabs(ball.position.x - laser.startX);
    return dx <= ball.radius && ball.position.y >= laser.position.y && ball.position.y <= player.position.y;
}

//bool CheckLaserCollision(const Meteor& meteor) {
//    if (!laser.isActive || meteor.hasExploded) return false;
//    float dist = sqrt(pow(laser.startX - meteor.position.x, 2) + pow(laser.position.y - meteor.position.y, 2));
//    return dist <= 20.0f;
//}

void ShootLaser() {
    int now = GetTime();
    if (!laser.isActive && now - player.lastShootTime >= SHOOT_COOLDOWN) {
        laser = { {player.position.x, player.position.y}, player.position.x, true };
        player.lastShootTime = now;
        /*for (size_t i = 0; i < meteors.size(); ++i) {
            if (!meteors[i].hasExploded && CheckLaserCollision(meteors[i])) {
                meteors[i].hasExploded = true;
                meteors[i].explosionStartTime = now;
                laser.isActive = false;
                break;
            }
        }*/
    }
}

void ActivateInvisibility() {
    if (player.remainingInvisibilityUses > 0 && !player.isInvisible) {
        player.isInvisible = true;
        player.invisibilityStartTime = GetTime();
        player.remainingInvisibilityUses--;
    }
}

void DropMeteor() {
    Meteor meteor = { {static_cast<float>(rand() % (SCREEN_WIDTH - 100) + 50), 0}, true, false, 0 };
    meteors.push_back(meteor);
    lastMeteorDropTime = GetTime();
}

void UpdateMeteors() {
    int now = GetTime();
    for (auto& meteor : meteors) {
        if (!meteor.hasExploded) {
            meteor.position.y += METEOR_FALL_SPEED;
            if (meteor.position.y >= SCREEN_HEIGHT - 10) {
                meteor.hasExploded = true;
                meteor.explosionStartTime = now;
            }
            if (meteor.hasExploded && !player.isInvisible) {
                float dist = sqrt(pow(player.position.x - meteor.position.x, 2) + pow(player.position.y - meteor.position.y, 2));
                if (dist < METEOR_EXPLOSION_RADIUS) {
                    gameOver = true;
                    SaveHighScore();
                }
            }
        }
    }
    meteors.erase(std::remove_if(meteors.begin(), meteors.end(),
        [](const Meteor& m) { return m.hasExploded && GetTime() - m.explosionStartTime > 1000; }), meteors.end());
}

void UpdateInvisibilityStatus() {
    if (player.isInvisible && GetTime() - player.invisibilityStartTime >= INVISIBILITY_DURATION) {
        player.isInvisible = false;
    }
}

void HandleInput() {
    player.isMoving = false;
    if (leftPressed) {
        player.position.x -= PLAYER_SPEED;
        player.isMoving = true;
        player.direction = 1.0f;
    }
    if (rightPressed) {
        player.position.x += PLAYER_SPEED;
        player.isMoving = true;
        player.direction = -1.0f;
    }
    if (spacePressed) ShootLaser();
    if (invisibilityPressed) { ActivateInvisibility(); invisibilityPressed = false; }
    player.position.x = std::max(player.width / 2, std::min(SCREEN_WIDTH - player.width / 2, player.position.x));
}

void UpdateScore() {
    int now = GetTime();
    if (now - lastScoreIncrementTime >= 1000) {
        score += 10;
        lastScoreIncrementTime = now;
        CheckBallSpawn();
        int invisibilityRewards = score / INVISIBILITY_REWARD_INTERVAL;
        int newRewards = invisibilityRewards - (lastInvisibilityRewardScore / INVISIBILITY_REWARD_INTERVAL);
        if (newRewards > 0) {
            player.remainingInvisibilityUses += newRewards;
            lastInvisibilityRewardScore = invisibilityRewards * INVISIBILITY_REWARD_INTERVAL;
        }
    }
}

void RenderShape(float x, float y, float width, float height, float r, float g, float b, float alpha, bool isCircle = false) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, alpha);
    if (isCircle) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(x, y);
        for (int i = 0; i <= 360; i += 10) {
            float angle = i * M_PI / 180;
            glVertex2f(x + cos(angle) * width, y + sin(angle) * height);
        }
        glEnd();
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i <= 360; i += 10) {
            float angle = i * M_PI / 180;
            glVertex2f(x + cos(angle) * width, y + sin(angle) * height);
        }
        glEnd();
    }
    else {
        glBegin(GL_QUADS);
        glVertex2f(x - width / 2, y - height / 2);
        glVertex2f(x + width / 2, y - height / 2);
        glVertex2f(x + width / 2, y + height / 2);
        glVertex2f(x - width / 2, y + height / 2);
        glEnd();
    }
    glDisable(GL_BLEND);
}

void RenderTexture(GLuint texture, float x, float y, float width, float height, float alpha, bool flipX = false) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    if (flipX) glScalef(-1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-width / 2, -height / 2);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(width / 2, -height / 2);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(width / 2, height / 2);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-width / 2, height / 2);
    glEnd();
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void RenderPlayer() {
    float alpha = player.isInvisible ? 0.3f : 1.0f;
    RenderTexture(ufoTexture, player.position.x, player.position.y, player.width, player.height, alpha, player.direction < 0);
    if (player.isInvisible) {
        float pulse = 0.5f + 0.5f * sin(GetTime() / 100.0f);
        RenderShape(player.position.x, player.position.y, player.width / 2 + 10, player.width / 2 + 10, 0.3f, 0.8f, 1.0f, 0.2f * pulse, true);
    }
}

void RenderBubbleEffect(const BubbleEffect& effect) {
    float pulse = 0.5f + 0.5f * sin(GetTime() / 100.0f);
    float radius = effect.radius * (1.0f + pulse * 0.2f * (1.0f - effect.alpha));
    if (effect.type == BUBBLE_EFFECT_BOUNCE) {
        RenderShape(effect.position.x, effect.position.y, radius, radius, 0.7f, 0.8f, 1.0f, effect.alpha * 0.6f, true);
    }
    else if (effect.type == BUBBLE_EFFECT_POP) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.9f, 0.95f, 1.0f, effect.alpha * 0.8f);
        glLineWidth(3.0f);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i <= 360; i += 20) {
            float angle = i * M_PI / 180;
            glVertex2f(effect.position.x + cos(angle) * radius, effect.position.y + sin(angle) * radius);
        }
        glEnd();
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
    }
    else {
        for (int i = 0; i < 5; i++) {
            float offsetX = cos(i * 72 * M_PI / 180) * radius * 0.5f;
            float offsetY = sin(i * 72 * M_PI / 180) * radius * 0.5f;
            float smallRadius = radius * (0.3f + 0.1f * sin(GetTime() / 100.0f + i));
            RenderShape(effect.position.x + offsetX, effect.position.y + offsetY, smallRadius, smallRadius, 0.8f, 0.9f, 1.0f, effect.alpha * 0.7f, true);
        }
    }
}

void RenderBubbleTrail(const Ball& ball) {
    float speed = sqrt(ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y);
    int numBubbles = static_cast<int>(speed / 3.0f);
    for (int i = 0; i < numBubbles; i++) {
        float t = static_cast<float>(i) / numBubbles;
        float offsetX = ball.velocity.x * t * -0.2f;
        float offsetY = ball.velocity.y * t * -0.2f;
        float size = ball.radius * (0.1f + 0.05f * sin(GetTime() / 100.0f + i * 10.0f));
        RenderShape(ball.position.x + offsetX, ball.position.y + offsetY, size, size, 0.8f, 0.9f, 1.0f, 0.3f * (1.0f - t), true);
    }
}

void UpdateBubbleEffects() {
    int now = GetTime();
    for (size_t i = 0; i < bubbleEffects.size();) {
        float elapsed = now - bubbleEffects[i].startTime;
        if (elapsed > 500.0f) {
            bubbleEffects.erase(bubbleEffects.begin() + i);
        }
        else {
            bubbleEffects[i].alpha = 1.0f - (elapsed / 500.0f);
            ++i;
        }
    }
}

void UpdateGame() {
    UpdateInvisibilityStatus();
    UpdateBubbleEffects();
    if (!gameOver) {
        UpdateScore();
        if (GetTime() - lastMeteorDropTime > METEOR_DROP_INTERVAL) DropMeteor();
    }
    UpdateMeteors();

    for (size_t i = 0; i < balls.size();) {
        bool wasMovingDown = balls[i].velocity.y > 0;
        balls[i].velocity.y += GRAVITY;
        balls[i].position.x += balls[i].velocity.x;
        balls[i].position.y += balls[i].velocity.y;

        if (balls[i].position.y + balls[i].radius > SCREEN_HEIGHT - 10) {
            balls[i].position.y = SCREEN_HEIGHT - 10 - balls[i].radius;
            balls[i].velocity.y *= -BALL_BOUNCE_FACTOR;
            if (wasMovingDown) {
                BubbleEffect effect = { balls[i].position, balls[i].radius * 0.8f, BUBBLE_EFFECT_BOUNCE, GetTime(), 1.0f };
                bubbleEffects.push_back(effect);
            }
        }
        if (balls[i].position.x - balls[i].radius < 0) {
            balls[i].position.x = balls[i].radius;
            balls[i].velocity.x *= -1.0f;
            BubbleEffect effect = { {balls[i].position.x - balls[i].radius, balls[i].position.y}, balls[i].radius * 0.5f, BUBBLE_EFFECT_BOUNCE, GetTime(), 1.0f };
            bubbleEffects.push_back(effect);
        }
        if (balls[i].position.x + balls[i].radius > SCREEN_WIDTH) {
            balls[i].position.x = SCREEN_WIDTH - balls[i].radius;
            balls[i].velocity.x *= -1.0f;
            BubbleEffect effect = { {balls[i].position.x + balls[i].radius, balls[i].position.y}, balls[i].radius * 0.5f, BUBBLE_EFFECT_BOUNCE, GetTime(), 1.0f };
            bubbleEffects.push_back(effect);
        }
        if (!player.isInvisible) {
            float dist = sqrt(pow(balls[i].position.x - player.position.x, 2) + pow(balls[i].position.y - player.position.y, 2));
            if (dist < balls[i].radius + player.width / 2) {
                gameOver = true;
                SaveHighScore();
                break;
            }
        }
        ++i;
    }

    if (laser.isActive) {
        laser.position.y -= LASER_SPEED;
        if (laser.position.y < 0) laser.isActive = false;
        for (size_t i = 0; i < balls.size(); ++i) {
            if (CheckLaserCollision(balls[i])) {
                BubbleEffect effect = { balls[i].position, balls[i].radius, BUBBLE_EFFECT_POP, GetTime(), 1.0f };
                bubbleEffects.push_back(effect);
                SplitBall(i);
                laser.isActive = false;
                break;
            }
        }
    }
}

void RenderMeteors() {
    for (const auto& meteor : meteors) {
        if (!meteor.hasExploded) {
            RenderTexture(meteorTexture, meteor.position.x, meteor.position.y, 40, 40, 1.0f);
        }
        else {
            float progress = (GetTime() - meteor.explosionStartTime) / 1000.0f;
            if (progress < 1.0f) {
                float radius = METEOR_EXPLOSION_RADIUS * progress;
                RenderShape(meteor.position.x, meteor.position.y, radius, radius, 1.0f, 0.3f, 0.1f, (1.0f - progress) * 0.7f, true);
                BubbleEffect effect = { meteor.position, radius, BUBBLE_EFFECT_POP, GetTime(), 1.0f };
                bubbleEffects.push_back(effect);
            }
        }
    }
}

void RenderUI() {
    char buffer[128];
    RenderShape(SCREEN_WIDTH - 110, 35, 200, 50, UI_PANEL_COLOR[0], UI_PANEL_COLOR[1], UI_PANEL_COLOR[2], UI_PANEL_COLOR[3]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(UI_HIGHLIGHT_COLOR[0], UI_HIGHLIGHT_COLOR[1], UI_HIGHLIGHT_COLOR[2], 0.7f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(SCREEN_WIDTH - 210, 10);
    glVertex2f(SCREEN_WIDTH - 10, 10);
    glVertex2f(SCREEN_WIDTH - 10, 60);
    glVertex2f(SCREEN_WIDTH - 210, 60);
    glEnd();
    glLineWidth(1.0f);
    glDisable(GL_BLEND);

    glColor3f(UI_TEXT_COLOR[0], UI_TEXT_COLOR[1], UI_TEXT_COLOR[2]);
    glRasterPos2f(SCREEN_WIDTH - 190, 35);
    snprintf(buffer, sizeof(buffer), "SCORE: %d", score);
    for (char* c = buffer; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
    glRasterPos2f(SCREEN_WIDTH - 190, 50);
    snprintf(buffer, sizeof(buffer), "HIGH SCORE: %d", highScore);
    for (char* c = buffer; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    RenderShape(105, 55, 190, 90, UI_PANEL_COLOR[0], UI_PANEL_COLOR[1], UI_PANEL_COLOR[2], UI_PANEL_COLOR[3]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(UI_HIGHLIGHT_COLOR[0], UI_HIGHLIGHT_COLOR[1], UI_HIGHLIGHT_COLOR[2], 0.7f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(10, 10);
    glVertex2f(200, 10);
    glVertex2f(200, 100);
    glVertex2f(10, 100);
    glEnd();
    glLineWidth(1.0f);
    glDisable(GL_BLEND);

    int pointsToNextBall = ((score / BALL_SPAWN_SCORE_INTERVAL) + 1) * BALL_SPAWN_SCORE_INTERVAL - score;
    glRasterPos2f(20, 25);
    snprintf(buffer, sizeof(buffer), "NEXT BALL: %d pts", pointsToNextBall);
    for (char* c = buffer; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
    float nextBallProgress = 1.0f - static_cast<float>(pointsToNextBall) / BALL_SPAWN_SCORE_INTERVAL;
    RenderShape(105, 40, 150, 10, 0.2f, 0.2f, 0.2f, 0.7f);
    if (nextBallProgress > 0) {
        RenderShape(105 - 75 + 75 * nextBallProgress, 40, 150 * nextBallProgress, 8, 0.9f, 0.6f, 0.1f, 0.9f);
    }
    glEnable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(30, 35);
    glVertex2f(180, 35);
    glVertex2f(180, 45);
    glVertex2f(30, 45);
    glEnd();
    glDisable(GL_BLEND);

    glRasterPos2f(20, 60);
    snprintf(buffer, sizeof(buffer), "INVISIBILITY: %d", player.remainingInvisibilityUses);
    for (char* c = buffer; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
    int pointsToNextReward = ((score / INVISIBILITY_REWARD_INTERVAL) + 1) * INVISIBILITY_REWARD_INTERVAL - score;
    glRasterPos2f(20, 75);
    snprintf(buffer, sizeof(buffer), "NEXT POWER: %d pts", pointsToNextReward);
    for (char* c = buffer; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
    float invisibilityProgress = 1.0f - static_cast<float>(pointsToNextReward) / INVISIBILITY_REWARD_INTERVAL;
    RenderShape(105, 85, 150, 10, 0.2f, 0.2f, 0.2f, 0.7f);
    if (invisibilityProgress > 0) {
        RenderShape(105 - 75 + 75 * invisibilityProgress, 85, 150 * invisibilityProgress, 8, 0.3f, 0.8f, 1.0f, 0.9f);
    }
    glEnable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(30, 80);
    glVertex2f(180, 80);
    glVertex2f(180, 90);
    glVertex2f(30, 90);
    glEnd();
    glDisable(GL_BLEND);

    if (player.isInvisible) {
        RenderShape(SCREEN_WIDTH / 2, SCREEN_HEIGHT - 40, 300, 30, UI_PANEL_COLOR[0], UI_PANEL_COLOR[1], UI_PANEL_COLOR[2], UI_PANEL_COLOR[3]);
        glEnable(GL_BLEND);
        glColor4f(UI_HIGHLIGHT_COLOR[0], UI_HIGHLIGHT_COLOR[1], UI_HIGHLIGHT_COLOR[2], 0.7f);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(SCREEN_WIDTH / 2 - 150, SCREEN_HEIGHT - 55);
        glVertex2f(SCREEN_WIDTH / 2 + 150, SCREEN_HEIGHT - 55);
        glVertex2f(SCREEN_WIDTH / 2 + 150, SCREEN_HEIGHT - 25);
        glVertex2f(SCREEN_WIDTH / 2 - 150, SCREEN_HEIGHT - 25);
        glEnd();
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
        int timeLeft = INVISIBILITY_DURATION - (GetTime() - player.invisibilityStartTime);
        glRasterPos2f(SCREEN_WIDTH / 2 - 140, SCREEN_HEIGHT - 45);
        snprintf(buffer, sizeof(buffer), "INVISIBILITY: %.1f SEC", timeLeft / 1000.0f);
        for (char* c = buffer; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
        float timePercentage = static_cast<float>(timeLeft) / INVISIBILITY_DURATION;
        RenderShape(SCREEN_WIDTH / 2, SCREEN_HEIGHT - 35, 240, 10, 0.2f, 0.2f, 0.2f, 0.7f);
        if (timePercentage > 0) {
            RenderShape(SCREEN_WIDTH / 2 - 120 + 120 * timePercentage, SCREEN_HEIGHT - 35, 240 * timePercentage, 8, 0.3f, 0.9f, 1.0f, 0.9f);
        }
        glEnable(GL_BLEND);
        glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(SCREEN_WIDTH / 2 - 120, SCREEN_HEIGHT - 40);
        glVertex2f(SCREEN_WIDTH / 2 + 120, SCREEN_HEIGHT - 40);
        glVertex2f(SCREEN_WIDTH / 2 + 120, SCREEN_HEIGHT - 30);
        glVertex2f(SCREEN_WIDTH / 2 - 120, SCREEN_HEIGHT - 30);
        glEnd();
        glDisable(GL_BLEND);
    }

    RenderShape(SCREEN_WIDTH / 2, SCREEN_HEIGHT - 15, 500, 20, UI_PANEL_COLOR[0], UI_PANEL_COLOR[1], UI_PANEL_COLOR[2], UI_PANEL_COLOR[3]);
    glEnable(GL_BLEND);
    glColor4f(UI_HIGHLIGHT_COLOR[0], UI_HIGHLIGHT_COLOR[1], UI_HIGHLIGHT_COLOR[2], 0.7f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(SCREEN_WIDTH / 2 - 250, SCREEN_HEIGHT - 25);
    glVertex2f(SCREEN_WIDTH / 2 + 250, SCREEN_HEIGHT - 25);
    glVertex2f(SCREEN_WIDTH / 2 + 250, SCREEN_HEIGHT - 5);
    glVertex2f(SCREEN_WIDTH / 2 - 250, SCREEN_HEIGHT - 5);
    glEnd();
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glRasterPos2f(SCREEN_WIDTH / 2 - 175, SCREEN_HEIGHT - 10);
    const char* controlText = "LEFT/RIGHT: Move | SPACE: Shoot | I: Invisibility | R: Restart";
    for (const char* c = controlText; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    if (gameOver) {
        RenderShape(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, SCREEN_WIDTH, SCREEN_HEIGHT, 0.0f, 0.0f, 0.0f, 0.7f);
        RenderShape(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 350, 200, UI_PANEL_COLOR[0], UI_PANEL_COLOR[1], UI_PANEL_COLOR[2], UI_PANEL_COLOR[3]);
        glEnable(GL_BLEND);
        glColor4f(UI_HIGHLIGHT_COLOR[0], UI_HIGHLIGHT_COLOR[1], UI_HIGHLIGHT_COLOR[2], 0.7f);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(SCREEN_WIDTH / 2 - 175, SCREEN_HEIGHT / 2 - 100);
        glVertex2f(SCREEN_WIDTH / 2 + 175, SCREEN_HEIGHT / 2 - 100);
        glVertex2f(SCREEN_WIDTH / 2 + 175, SCREEN_HEIGHT / 2 + 100);
        glVertex2f(SCREEN_WIDTH / 2 - 175, SCREEN_HEIGHT / 2 + 100);
        glEnd();
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
        glColor3f(1.0f, 0.3f, 0.3f);
        glRasterPos2f(SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2 - 70);
        const char* gameOverText = "GAME OVER";
        for (const char* c = gameOverText; *c; c++) glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, *c);
        glColor3f(UI_TEXT_COLOR[0], UI_TEXT_COLOR[1], UI_TEXT_COLOR[2]);
        glRasterPos2f(SCREEN_WIDTH / 2 - 70, SCREEN_HEIGHT / 2 - 20);
        snprintf(buffer, sizeof(buffer), "FINAL SCORE: %d", score);
        for (char* c = buffer; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
        if (score >= highScore && score > 0) {
            glColor3f(1.0f, 1.0f, 0.0f);
            glRasterPos2f(SCREEN_WIDTH / 2 - 120, SCREEN_HEIGHT / 2 + 10);
            const char* highScoreText = "NEW HIGH SCORE ACHIEVED!";
            for (const char* c = highScoreText; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
        }
        glColor3f(UI_TEXT_COLOR[0], UI_TEXT_COLOR[1], UI_TEXT_COLOR[2]);
        glRasterPos2f(SCREEN_WIDTH / 2 - 70, SCREEN_HEIGHT / 2 + 50);
        const char* restartText = "Press 'R' to restart";
        for (const char* c = restartText; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
    }
}

void RenderGame() {
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(BG_COLOR[0], BG_COLOR[1], BG_COLOR[2], 1.0f);
    RenderTexture(backgroundTexture, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, SCREEN_WIDTH, SCREEN_HEIGHT, 1.0f);
    RenderShape(SCREEN_WIDTH / 2, SCREEN_HEIGHT - 5, SCREEN_WIDTH, 10, GROUND_COLOR[0], GROUND_COLOR[1], GROUND_COLOR[2], 1.0f);
    RenderShape(SCREEN_WIDTH / 2, SCREEN_HEIGHT - 10, SCREEN_WIDTH, 5, GROUND_COLOR[0] * 0.7f, GROUND_COLOR[1] * 0.7f, GROUND_COLOR[2] * 0.7f, 1.0f);
    RenderPlayer();
    for (const auto& ball : balls) RenderBubbleTrail(ball);
    for (const auto& effect : bubbleEffects) RenderBubbleEffect(effect);
    for (const auto& ball : balls) {
        RenderShape(ball.position.x, ball.position.y, ball.radius, ball.radius, 0.6f, 1.0f, 0.6f, 0.7f, true);
        RenderShape(ball.position.x - ball.radius * 0.3f, ball.position.y - ball.radius * 0.3f, ball.radius * 0.3f, ball.radius * 0.3f, 1.0f, 1.0f, 1.0f, 0.3f, true);
    }
    RenderMeteors();
    if (laser.isActive) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(3.0f);
        glBegin(GL_LINES);
        glColor4f(0.0f, 1.0f, 0.0f, 1.0f); // Green laser
        glVertex2f(laser.startX, player.position.y - player.height / 2); // Start from the center of the UFO
        glVertex2f(laser.startX, laser.position.y);
        glEnd();
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
    }
    RenderUI();
}

void Display() {
    HandleInput();
    if (!gameOver) UpdateGame();
    RenderGame();
    glutSwapBuffers();
}

void Timer(int) {
    glutPostRedisplay();
    glutTimerFunc(16, Timer, 0);
}

void RestartGame() {
    balls.clear();
    bubbleEffects.clear();
    meteors.clear();
    player = { {SCREEN_WIDTH / 2, SCREEN_HEIGHT - 50}, false, 80, 130, 0, false, 0, MAX_INVISIBILITY_USES, 1.0f };
    laser = { {0, 0}, 0, false };
    score = 0;
    gameOver = false;
    gameStartTime = GetTime();
    lastScoreIncrementTime = gameStartTime;
    lastInvisibilityRewardScore = 0;
    lastBallSpawnScore = 0;
    SpawnBall(SCREEN_WIDTH / 2, 100, 30);
}

void KeyDown(unsigned char key, int, int) {
    switch (key) {
    case 'a': case 'A': leftPressed = true; break;
    case 'd': case 'D': rightPressed = true; break;
    case ' ': spacePressed = true; break;
    case 'i': case 'I': invisibilityPressed = true; break;
    case 'r': case 'R': if (gameOver) RestartGame(); break;
    }
}

void KeyUp(unsigned char key, int, int) {
    switch (key) {
    case 'a': case 'A': leftPressed = false; break;
    case 'd': case 'D': rightPressed = false; break;
    case ' ': spacePressed = false; break;
    }
}

void SpecialDown(int key, int, int) {
    if (key == GLUT_KEY_LEFT) leftPressed = true;
    else if (key == GLUT_KEY_RIGHT) rightPressed = true;
}

void SpecialUp(int key, int, int) {
    if (key == GLUT_KEY_LEFT) leftPressed = false;
    else if (key == GLUT_KEY_RIGHT) rightPressed = false;
}

GLuint LoadTexture(const char* filename) {
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 0);
    if (!data) {
        std::cerr << "Failed to load texture: " << filename << std::endl;
        return 0;
    }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    GLenum format = channels == 3 ? GL_RGB : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    gluBuild2DMipmaps(GL_TEXTURE_2D, format, width, height, format, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    return texture;
}

void InitializeGame() {
    player = { {SCREEN_WIDTH / 2, SCREEN_HEIGHT - 50}, false, 80, 130, 0, false, 0, MAX_INVISIBILITY_USES, 1.0f };
    gameOver = false;
    score = 0;
    gameStartTime = GetTime();
    lastScoreIncrementTime = gameStartTime;
    lastInvisibilityRewardScore = 0;
    lastBallSpawnScore = 0;
    srand(static_cast<unsigned>(time(nullptr)));
    backgroundTexture = LoadTexture("background.jpg");
    ufoTexture = LoadTexture("ufo.png");
    meteorTexture = LoadTexture("meteor.png");
    LoadHighScore();
    SpawnBall(SCREEN_WIDTH / 2, 100, 30);
}

void InitOpenGL() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(SCREEN_WIDTH, SCREEN_HEIGHT);
    glutCreateWindow("UFO STRIKE");
    InitOpenGL();
    InitializeGame();
    glutDisplayFunc(Display);
    glutTimerFunc(16, Timer, 0);
    glutKeyboardFunc(KeyDown);
    glutKeyboardUpFunc(KeyUp);
    glutSpecialFunc(SpecialDown);
    glutSpecialUpFunc(SpecialUp);
    glutMainLoop();
    return 0;
}