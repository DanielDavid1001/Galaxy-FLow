#include <stdio.h>
#include <raylib.h>
#include <raymath.h>
#include "pthread.h" 
#include <stdatomic.h> 
#include <time.h>  

pthread_t threadId = {0};  // ID da thread de carregamento(Loading Screen)
bool loadingStarted = false;
int framesCounter = 0;
bool Paused = false; // Variável para controlar o estado de pausa

typedef enum { TELA_MENU, TELA_JOGO, TELA_LOADING} GameScreen;//Telas do jogo
//Loading recursos Globais
static atomic_int dataProgress = 0;
static atomic_bool dataLoaded = false;// Variável atômica para indicar se os dados foram carregados
static void *LoadDataThread(void *arg);// Variável atômica para o progresso do carregamento
// Menu recursos Globais
static GameScreen currentScreen = TELA_MENU; //Inicializa na tela menu
static bool mute = false; // Mute global 
static bool exitWindow = false; // Flag para fechar a janela

// Botões do menu (precisam ser Retangulos)
static Rectangle btnPlay = { 300, 200, 200, 50,}; // Botão de jogar (Iniciar Jogo)
static Rectangle btnExit = { 300, 270, 200, 50,}; // Botão de sair (Sair do Jogo)
static Rectangle btnSound = { 760, 430, 40, 40,}; // Botão de som (Ativar/Desativar Som do Menu)

typedef enum { // Formatos de inimigos e Disparos
    FORMATO_CIRCULO,
    FORMATO_RETANGULO,
    FORMATO_SPRITE
} Forma;

// Representa os tipos de armas
typedef enum {
    RAIL_GUN,
    RAIL_RIFLE,
    RAIL_CANNON
} TipoArma;

static const int DANO_ARMA[3] = {10, 13, 30}; // Dano das armas

// Disparos da nave
typedef struct {
    Vector2 position;
    float speed;
    bool active;
    Forma forma;
} Tiro;

// Inimigos
typedef struct {
    Vector2 position;
    float speed;
    int vida;
    bool active;
    Texture2D sprite;
    Forma forma;
} Inimigo;

// Explosões
typedef struct {
    Vector2 pos;
    float timer;
    int currentFrame;
    bool active;
} Explosion;

#define MAX_INIMIGOS 64
static Inimigo inimigos[MAX_INIMIGOS] = {0};// Inicia que os inimigos estão inativos
Texture2D inimigoTexture;

#define MAX_EXPLOSIONS 20
#define MAX_INIMIGOS_ATIVOS 10
#define MAX_TIROS 100

static Explosion explosions[MAX_EXPLOSIONS] = {0};// Inicia que as explosões estão inativas
static float TempoUltimoSpawn = 0.0f; // Guarda o tempo do último spawn
static const float IntervaloSpawn = 1.0f; // Spawna a cada 1 segundo
TipoArma armaAtual = RAIL_GUN; // Arma padrão
Sound trocaRailGun, trocaRailRifle, trocaRailCannon;

// Variáveis de estado (globais/estáticas)
static Music menuMusic;
static Color soundColor = GREEN;
static Music gameMusic;

// Função para tela de Loading
static void *LoadDataThread(void *arg)
{
    int timeCounter = 0;
    clock_t prevTime = clock();
    while (timeCounter < 5000)
    {
        clock_t currentTime = clock() - prevTime;
        timeCounter = currentTime*1000/CLOCKS_PER_SEC;
        atomic_store_explicit(&dataProgress, timeCounter/10, memory_order_relaxed); // Atualiza o progresso do carregamento
    }
    atomic_store_explicit(&dataLoaded, true, memory_order_relaxed);
    return NULL;
}

// Função para alternar mudo ou não
void ToggleMute(void)
{
    mute = !mute;// Define que ação de mutar não foi feita
    if (mute)
    {   soundColor = RED; //Muda a cor do botão de som para inativo
        SetMasterVolume(0.0f); // Muda o volume para 0
        PauseMusicStream(menuMusic); // Pausa em vez de parar
    }
    else
    {   soundColor = GREEN; // Muda a cor do botão de som Para ativo
        SetMasterVolume(0.5f); // Muda o volume para Maximo
        ResumeMusicStream(menuMusic); // Retoma de onde parou
    }
}
// Função para obter um slot livre para inimigos
int ObterSlotLivre(void) {
    for (int i = 0; i < MAX_INIMIGOS; i++) {
        if (!inimigos[i].active) return i;// Verifica se tem slots vazios. Caso tenha, retorna o índice do slot vazio
    }// Caso não ache retorna -1
    return -1; 
}// Funçao para criar inimigos(Só roda se houver slot livre)
void CriarInimigo(Vector2 pos, float speed) {
    int slot = ObterSlotLivre();
    if (slot < 0) return;  // não há espaço livre na função acima e não executa nada
 // Inicializa as propriedades do inimigo no slot encontrado
    inimigos[slot].position = pos;
    inimigos[slot].speed = speed;
    inimigos[slot].vida = 30;
    inimigos[slot].active = true;

    inimigos[slot].sprite = inimigoTexture;// Define que este inimigo usa sprite
    inimigos[slot].forma  = FORMATO_SPRITE;// Define a forma do inimigo como sprite
}
// Cria um disparo na posição original
void CriarDisparo(Tiro tiros[], int index, Vector2 origem, float offsetX, float speed) {
    if (index < MAX_TIROS && !tiros[index].active) {// Chequa se o índice está dentro do limite e se o tiro não está ativo
        tiros[index].position = (Vector2){ origem.x + offsetX, origem.y };
        tiros[index].speed = speed;
        tiros[index].active = true;

        tiros[index].forma = FORMATO_CIRCULO;// Define a forma do disparo como círculo
    }
}
// Verifica colisão entre um tiro e um inimigo, com formatos e raios/hitbox 
bool VerificarColisao(Tiro tiro, Inimigo inimigo, float raioTiro, float raioInimigo, Rectangle hitboxInimigo) {
    if (!tiro.active || !inimigo.active) return false;

    // tiro círculo vs inimigo círculo(Rail_Cannon)
    if (tiro.forma == FORMATO_CIRCULO && inimigo.forma == FORMATO_CIRCULO)
        return CheckCollisionCircles(tiro.position, raioTiro, inimigo.position, raioInimigo);

    // tiro círculo vs inimigo retângulo(Rail_GUN)
    if (tiro.forma == FORMATO_CIRCULO && inimigo.forma == FORMATO_RETANGULO)
        return CheckCollisionCircleRec(tiro.position, raioTiro, hitboxInimigo);

    // tiro retângulo vs inimigo retângulo(RAIL_RIFLE)
    if (tiro.forma == FORMATO_RETANGULO && inimigo.forma == FORMATO_RETANGULO) {
        Rectangle hitboxTiro = { tiro.position.x, tiro.position.y, 4, 12 };
        return CheckCollisionRecs(hitboxTiro, hitboxInimigo);
    }
    // tiro retângulo vs inimigo sprite(Somente Rail_RIFLE)
    if (tiro.forma == FORMATO_RETANGULO && inimigo.forma == FORMATO_SPRITE) {
        Rectangle hitboxTiro = { tiro.position.x, tiro.position.y, 4, 12 };
        // centraliza a hitbox(hbSprite) do sprite
        Rectangle hbSprite = {
            inimigo.position.x - inimigo.sprite.width/2,// Centraliza a largura
            inimigo.position.y - inimigo.sprite.height/2,// Centraliza a altura
            inimigo.sprite.width,
            inimigo.sprite.height
        };
        return CheckCollisionRecs(hitboxTiro, hbSprite);// Caso haja colisão retorna true
    }
    return false;// Caso não haja colisão retorna false
}
// Ativa uma nova explosão no vetor
void CreateExplosion(Vector2 position) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {// COnta quantas explosões ativas existem
        if (!explosions[i].active) {// Se tiver um slot vazio ele executa
            explosions[i].pos = position;
            explosions[i].timer = 0.0f;
            explosions[i].currentFrame = 0;
            explosions[i].active = true;
            break;
        }
    }
}
int main(void)
{   // Fecha o jogo
    SetExitKey(KEY_NULL); // desativa a tecla de saída padrão
    bool exitWindowRequested = false;
    bool EndGame = false;
    //Define a vida do Player e seus pontos iniciais
    int vida = 100, 
    pontos = 0;

    InitAudioDevice();
    SetMasterVolume(0.5f); // Ajuste inicial

    // Carrega música de fundo do menu
    Music menuMusic = LoadMusicStream("Galaxy-Tech_Final_Version.wav");// Colaboração de Daniel David(Compositor Primario) e Julia Sampaio(Compositora e Avaliadora)
    PlayMusicStream(menuMusic);
    Music gameMusic = LoadMusicStream("BasicSpaceTrip.wav"); // De Daniel David (Compositor)
    PlayMusicStream(gameMusic);

    // Cria a janela do jogo
    InitWindow(820, 500, "GAME - Galaxy Flow");

    pthread_t threadID = {0};// Carrega o ID da thread

    int larguraTela = GetScreenWidth();
    int alturaTela  = GetScreenHeight();

    // Cria a Nave e carrega textura
    Texture2D sprite = LoadTexture("nave.png"); // Carrega a textura em .png da NAVE
    printf("Largura: %d, Altura: %d\n", sprite.width, sprite.height);

    // Inicializa vetores e sons
    Tiro tiros[MAX_TIROS] = {0}; // Inicializa todos como inativos
    Sound tiroSound1 = LoadSound("tiro.wav");
    Sound tiroSound2 = LoadSound("tiro2.wav");
    Sound tiroSound3 = LoadSound("tiro3.wav");
    trocaRailGun = LoadSound("troca_rail_gun.wav");
    trocaRailRifle = LoadSound("troca_rail_rifle.wav");
    trocaRailCannon = LoadSound("troca_rail_cannon.wav");
    Sound impactoSound = LoadSound("impacto.wav");

    // Texturas e frames de explosão
    Texture2D TexturaExplosao = LoadTexture("explosao.png");
    const int EXP_FRAMES = 8;
    const float EXP_FRAME_TIME = 0.5f;
    int frameWidth = 90;
    int frameHeight = 90;
    Texture2D game_over = LoadTexture("game_over.png");
    
    // Textura do inimigo
    inimigoTexture = LoadTexture("inimigo.png");

    // Posição inicial da nave
    Vector2 position = { 400, 400 };

    int animeFrames2 = 5; // Contador de frames para animação do fundo do game
    // Carrega todo gif em uma única imagem
    Image Anima2 = LoadImageAnim ("space_back.gif", &animeFrames2);
    Texture2D textAnim2 = LoadTextureFromImage(Anima2);
    
    unsigned int nextFrameDataOffset = 0; // Byte atual saí para o próximo frame dentro da image.data memory

    int animeFrames = 5; // Contador de frames para animação do menu
    // Carrega todo o gif em uma unica imagem
    Image Anima = LoadImageAnim ("space_anima.gif", &animeFrames);
    Texture2D textAnim = LoadTextureFromImage(Anima);

    int currentAnimFrame = 0; // Frame atual da animação
    int frameDelay = 8; // Delay entre frames da animação
    int frameCounter = 0; // Contador de frames para animação

    SetTargetFPS(60);// Fps do jogo 

    // HUD
    int hitsRecebidos = 0;
    int larguraBarra = 200;
    int alturaBarra = 20;
    int xBarra = 20;
    int yBarra = 20;

    // Indica a arma anterior (para som de troca)
    TipoArma armaAnterior = armaAtual;
    
    float posx = GetScreenHeight();

    // Loop principal
    while (!exitWindow)  // Continua até sair
    { if (IsKeyPressed(KEY_F11))// Ativa fullscreen com F11
            {
                if (IsWindowFullscreen())
                {
                    ToggleFullscreen();  // volta ao modo janela
                    SetWindowSize(820, 500); // reajusta janela
                }
                else
                {
                    ToggleFullscreen();     // entra em tela cheia
                }
            }
         // Switch entre telas
        switch(currentScreen){
        // Tela de Menu
        case TELA_MENU:
        if (currentScreen == TELA_MENU || currentScreen == TELA_LOADING){
           if (!IsMusicValid(menuMusic)) PlayMusicStream(menuMusic);
            UpdateMusicStream(menuMusic);

            frameCounter++;
            if (frameCounter >= frameDelay){
                currentAnimFrame++;
                if (currentAnimFrame >= animeFrames) currentAnimFrame = 0; // Reseta o frame atual
              int frameSize = Anima.width * Anima.height * 4;// Avança para o próximo frame
              UpdateTexture(textAnim, ((unsigned char *)Anima.data) + (currentAnimFrame * frameSize));// Atualiza a textura com o próximo frame
                frameCounter = 0; // Reseta o contador de frames
            }
        if (currentScreen == TELA_MENU)
        {    Vector2 mouse = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
            if (CheckCollisionPointRec(mouse, btnPlay)) {
                atomic_store_explicit(&dataLoaded, false, memory_order_relaxed);
                atomic_store_explicit(&dataProgress, 0, memory_order_relaxed);
                pthread_create(&threadId, NULL, LoadDataThread, NULL);
                currentScreen = TELA_LOADING; // Muda para a tela de loading
                }
               if (CheckCollisionPointRec(mouse, btnPlay))  currentScreen = TELA_LOADING;
               if (CheckCollisionPointRec(mouse, btnExit))  exitWindow = true;
               if (CheckCollisionPointRec(mouse, btnSound)) ToggleMute();
            }
           
        // Drawing da tela de menu
        BeginDrawing();
        ClearBackground(BLACK); 
        DrawTexture(textAnim, GetScreenWidth()/2 + textAnim.height/2, 200,WHITE);
        DrawText("Galaxy Flow", 281, 100, 40, WHITE);
        DrawRectangleRec(btnPlay, DARKBLUE);
        DrawText("PLAY", btnPlay.x + 70, btnPlay.y + 10, 20, GREEN);
        DrawRectangleRec(btnExit, GRAY);
        DrawText("EXIT", btnExit.x + 70, btnExit.y + 10, 20, RED);
        DrawRectangleRec(btnSound, soundColor);
        DrawText(mute ? "DESL" : "LIG", btnSound.x + -60, btnSound.y + 10, 20, WHITE);// Texto do botão de som(Com operador ternario 😒)
   
        EndDrawing();
        }
        break;

        case TELA_LOADING:
        if (currentScreen == TELA_LOADING) {
            if (!loadingStarted) {
                loadingStarted = true;
                atomic_store_explicit(&dataLoaded, false, memory_order_relaxed);
                atomic_store_explicit(&dataProgress, 0, memory_order_relaxed);
                pthread_create(&threadId, NULL, LoadDataThread, NULL);
            }
            framesCounter++;
            if (atomic_load_explicit(&dataLoaded, memory_order_relaxed)) {
                pthread_join(threadId, NULL);
                currentScreen = TELA_JOGO;
                framesCounter = 0;
            }
        } 
        UpdateMusicStream(menuMusic);

        BeginDrawing();
        ClearBackground(BLACK);
        // Desenha a barra de progresso
        if (currentScreen == TELA_LOADING) {
            DrawRectangle(150, 200, atomic_load_explicit(&dataProgress, memory_order_relaxed), 60, GREEN);
            if ((framesCounter / 15) % 2) DrawText("LOADING...", 240, 210, 40, WHITE);
            DrawRectangleLines(150, 200, 500, 60, GRAY);
            DrawText("Aperte ESC no jogo para sair",60, 450, 20, WHITE);
        } else if (currentScreen == TELA_JOGO) {}
        EndDrawing();
        break;
}
        case TELA_JOGO:
        if (!EndGame)
        {
            if (IsKeyPressed(KEY_SPACE))
            Paused = !Paused; // Pausa o jogo com a tecla de espaço
            if (!Paused){
                framesCounter++;
                if(framesCounter >= frameDelay){
                    currentAnimFrame++;
                    if(currentAnimFrame >= animeFrames2) currentAnimFrame = 0; // Reseta o frame atual
                    int frameSize = Anima2.width * Anima2.height * 4; // Avança para o próximo frame
                    UpdateTexture(textAnim2, ((unsigned char *)Anima2.data) + (currentAnimFrame * frameSize)); // Atualiza a textura com o próximo frame
                    frameCounter = 0;   
                }
        // Inicializa a música do jogo 
            if (!IsMusicValid(gameMusic)) PlayMusicStream(gameMusic);
            UpdateMusicStream(gameMusic);

        // Spawn automático de inimigos
            int contAtivos = 0;
            for (int i = 0; i < MAX_INIMIGOS; i++)
                if (inimigos[i].active) contAtivos++;

            float tempoAtual = GetTime();
            if (contAtivos < MAX_INIMIGOS_ATIVOS && (tempoAtual - TempoUltimoSpawn) > IntervaloSpawn)
            {
                for (int i = 0; i < MAX_INIMIGOS; i++)
                {
                    if (!inimigos[i].active)
                    {
                        Vector2 pos = { GetRandomValue(0, larguraTela - inimigoTexture.width), -(float)inimigoTexture.height};
                        CriarInimigo(pos, GetRandomValue(2, 4));
                        TempoUltimoSpawn = tempoAtual;
                        break;
                    }
                }
            }
            // Atualiza posição dos inimigos
            for (int i = 0; i < MAX_INIMIGOS; i++)
            {
                if (inimigos[i].active)
                {
                    inimigos[i].position.y += inimigos[i].speed;
                    if (inimigos[i].position.y > alturaTela)
                        inimigos[i].active = false; // sai da tela
                }
            }
            // Verifica colisões tiro → inimigo
            for (int i = 0; i < MAX_INIMIGOS; i++)
            {
                if (!inimigos[i].active) continue;
                Rectangle hitboxInimigo = {
                    inimigos[i].position.x,
                    inimigos[i].position.y,
                    40, 40
    };
    // Atribuição de raio ou hitbox para o tiro
    float raioTiro;
    if (armaAtual == RAIL_CANNON)
        raioTiro = 6;
    else
        raioTiro = 3;
    for (int j = 0; j < MAX_TIROS; j++)
    {
        if (!tiros[j].active) continue;

        if (CheckCollisionCircleRec(tiros[j].position, raioTiro, hitboxInimigo))
        {
            inimigos[i].vida -= DANO_ARMA[armaAtual];
            tiros[j].active = false;
            pontos += 25;
            if (inimigos[i].vida <= 0)
            {
                inimigos[i].active = false;
                Vector2 center = {
                    hitboxInimigo.x + hitboxInimigo.width/2 - 45,
                    hitboxInimigo.y + hitboxInimigo.height/2 - 45
                };
                CreateExplosion(center);
            }
            break;
        }
    }
}
            // Colisão inimigo → jogador
            Rectangle hitboxJogador = {position.x, position.y, (float)sprite.width, (float)sprite.height};
            for (int i = 0; i < MAX_INIMIGOS; i++)
            {
                if (!inimigos[i].active) continue;
                Rectangle hitboxInimigo = {inimigos[i].position.x, inimigos[i].position.y, 40, 40};
                if (CheckCollisionRecs(hitboxJogador, hitboxInimigo))
                {
                    vida -= 25;
                    inimigos[i].active = false;
                    PlaySound(impactoSound);
                    if (vida <= 0)
                    {
                        vida = 0;
                        EndGame = true;
                    }
                }
            }
            // Atualiza explosões ativas
            for (int i = 0; i < MAX_EXPLOSIONS; i++)
            {
                if (!explosions[i].active) continue;
                explosions[i].timer += GetFrameTime();
                if (explosions[i].timer > EXP_FRAME_TIME)
                {
                    explosions[i].timer = 0.0f;
                    explosions[i].currentFrame++;
                    if (explosions[i].currentFrame >= EXP_FRAMES)
                        explosions[i].active = false;
                }
            }
            // Troca de arma
            if (IsKeyDown(KEY_ONE)) armaAtual = RAIL_GUN;
            if (IsKeyDown(KEY_TWO)) armaAtual = RAIL_RIFLE;
            if (IsKeyDown(KEY_THREE)) armaAtual = RAIL_CANNON;

            // Som na troca de arma
            if (armaAtual != armaAnterior)
            {
                switch (armaAtual)
                {
                    case RAIL_GUN: PlaySound(trocaRailGun);   break;
                    case RAIL_RIFLE: PlaySound(trocaRailRifle); break;
                    case RAIL_CANNON: PlaySound(trocaRailCannon);break;
                }
                armaAnterior = armaAtual;
            }
            // Disparo com F
            if (IsKeyPressed(KEY_F))
            {
                for (int i = 0; i < MAX_TIROS; i++)
                {
                    if (!tiros[i].active)
                    {
                        switch (armaAtual)
                        {
                            case RAIL_GUN:
                                CriarDisparo(tiros, i, position,
                                sprite.width/2.0f - 2.5f, 10);
                                tiros[i].forma = FORMATO_CIRCULO;
                                PlaySound(tiroSound1);
                                break;
                            case RAIL_RIFLE:
                                if (i + 3 < MAX_TIROS)
                                {
                                    CriarDisparo(tiros, i, position, sprite.width*0.2f, 20);
                                    CriarDisparo(tiros, i + 1, position, sprite.width*0.3f, 20);
                                    CriarDisparo(tiros, i + 2, position, sprite.width*0.6f, 20);
                                    CriarDisparo(tiros, i + 3, position, sprite.width*0.7f, 20);
                                    tiros[i].forma = FORMATO_RETANGULO;
                                    PlaySound(tiroSound2);
                                }
                                break;
                            case RAIL_CANNON:
                                CriarDisparo(tiros, i, position, sprite.width/2.0f - 2.5f, 10);
                                PlaySound(tiroSound3);
                                break;
                        }
                        break; // só um tiro por pressionamento
                    }
                }
            }
            // Atualiza posição dos tiros ativos
            for (int i = 0; i < MAX_TIROS; i++)
            {
                if (!tiros[i].active) continue;
                tiros[i].position.y -= tiros[i].speed;
                if (tiros[i].position.y < 0)
                    tiros[i].active = false; // sai da tela
            }
            // Movimento do jogador ↑ ← ↓ →
            if (IsKeyDown(KEY_RIGHT)) position.x += 5;
            if (IsKeyDown(KEY_LEFT))  position.x -= 5;
            if (IsKeyDown(KEY_UP))    position.y -= 5;
            if (IsKeyDown(KEY_DOWN))  position.y += 5;

            // Limita nave à janela
            position.x = Clamp(position.x, 0, GetScreenWidth() - sprite.width);
            position.y = Clamp(position.y, 0, GetScreenHeight() - sprite.height); 
            UpdateMusicStream(gameMusic); 
        }
         
    }       // Início dos desenhos principais
        BeginDrawing();
        ClearBackground(WHITE);
        DrawTexturePro(textAnim2,
            (Rectangle){0, 0, textAnim2.width, textAnim2.height},
            (Rectangle){0, 0, larguraTela, alturaTela *1.3 },
            (Vector2){0, 0}, 0, WHITE);
        // Pede confirmação para fechar a janela
            if (IsKeyPressed(KEY_ESCAPE) || WindowShouldClose()) exitWindowRequested = true;
            if (exitWindowRequested) {
            if (IsKeyPressed(KEY_S)) exitWindow = true;
            else if (IsKeyPressed(KEY_N)) exitWindowRequested = false;
          } 

        if (exitWindowRequested)
            { DrawRectangle(0, 150, larguraTela, 120, DARKGREEN);
                DrawText("Você quer sair do jogo? [S/N]", 80, 180, 30, WHITE);
            }else if (!EndGame){

            // Desenha inimigos ativos
    for (int i = 0; i < MAX_INIMIGOS; i++) {
        if (!inimigos[i].active) continue;

    if (inimigos[i].forma == FORMATO_SPRITE) {
        int x = (int)(inimigos[i].position.x - inimigos[i].sprite.width/2);
        int y = (int)(inimigos[i].position.y - inimigos[i].sprite.height/2);
        DrawTexture(inimigos[i].sprite, x, y, WHITE);
    }
    else {  
        DrawCircleV(inimigos[i].position, 20, GREEN);
    }
}           // Desenha nave
            DrawTexture(sprite, position.x, position.y, WHITE);
            // Desenha tiros
            for (int i = 0; i < MAX_TIROS; i++)
            {
                if (!tiros[i].active) continue;
                switch (armaAtual)
                {   case RAIL_GUN:
                        DrawCircle(tiros[i].position.x, tiros[i].position.y, 3, YELLOW);
                        break;
                    case RAIL_RIFLE:
                        DrawRectangle(tiros[i].position.x, tiros[i].position.y, 4, 12, BLUE);
                        break;
                    case RAIL_CANNON:
                        DrawCircle(tiros[i].position.x, tiros[i].position.y, 6, RED);
                        break;
                }
            }
            // Desenha HUD
            DrawRectangle(xBarra, yBarra, larguraBarra, alturaBarra, GRAY);
            DrawRectangle(xBarra, yBarra, (vida * larguraBarra) / 100, alturaBarra, RED);
            DrawText(TextFormat("Pontos: %d", pontos), 20, 50, 20, BLUE);
            // Nome da arma na HUD
            const char *nomeArma = "RAIL GUN";
            if (armaAtual == RAIL_RIFLE)  nomeArma = "RAIL RIFLE";
            if (armaAtual == RAIL_CANNON) nomeArma = "RAIL CANNON";
            DrawText(TextFormat("Arma: %s", nomeArma), 20, 80, 20, DARKGRAY);

            // Desenha explosões
            for (int i = 0; i < MAX_EXPLOSIONS; i++)
            {
                if (!explosions[i].active) continue;
                Rectangle src = {
                (float)(explosions[i].currentFrame * frameWidth), 0, (float)frameWidth, (float)frameHeight};
                DrawTextureRec(TexturaExplosao, src, explosions[i].pos, WHITE);
            }
            if(Paused)
            DrawText("PAUSE", 600, 38, 40, LIGHTGRAY);
        }   
        else
        {
            
            // Tela de Game Over
        DrawTexture(game_over, larguraTela/2 - game_over.width/2, alturaTela/2 - game_over.height/2, WHITE);
        DrawText("GAME OVER", larguraTela/2 - MeasureText("GAME OVER", 40)/2, alturaTela/2, 40, WHITE);
        DrawText("Pressione ENTER para sair", larguraTela/2 - 160, alturaTela/2 + 70, 25, WHITE);
        if (IsKeyPressed(KEY_ENTER)) {
            exitWindow = true; // Fecha o jogo ao apertar ENTER após Game Over
}
        }
        // Fim do desenho
        EndDrawing();
        break;
    }
  }
   // Sessão de descarrego 😈
    UnloadSound(trocaRailGun);
    UnloadSound(trocaRailRifle);
    UnloadSound(trocaRailCannon);
    UnloadSound(impactoSound);
    UnloadTexture(TexturaExplosao);
    UnloadTexture(game_over);
    UnloadTexture(inimigoTexture);
    UnloadMusicStream(menuMusic);
    CloseAudioDevice();
    CloseWindow();

    return 0;
} 