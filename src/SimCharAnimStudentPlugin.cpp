// Güvenlik uyarılarını kapatır (C++'ın eski fonksiyonlar için verdiği uyarıları gizler)
#define _CRT_SECURE_NO_WARNINGS
// Windows API'sini daha hafif ve hızlı yüklemek için gereksiz kütüphaneleri dışarıda bırakır
#define WIN32_LEAN_AND_MEAN
// Klavye dinleme (GetAsyncKeyState) fonksiyonunu kullanabilmemiz için Windows kütüphanesi

#define NOMINMAX

#include <windows.h>
#include <algorithm>
// Eklentimizin kendi başlık dosyası
#include "SimCharAnimStudentPlugin.h"

// Oyun motorunun (N8RO) bize sunduğu SDK (Yazılım Geliştirme Kiti) kütüphaneleri
#include <model/AnimationModel.h>       // Animasyonları ezmek için
#include <model/NavigationModel.h>      // Karakterin dünyadaki hareketini sağlamak için
#include <model/ModelFactoryRegistry.h> // Kendi sınıflarımızı motora kaydetmek için
#include <plugin/IModelPluginService.h> // Motorun servislerine erişim
#include <plugin/PluginContext.h>       // Eklenti bağlamı
#include <plugin/IPluginServices.h>     // Eklenti servisleri
#include <core/json/JsonValue.h>        // JSON formatında veri okuma/yazma (Motor bunu bekliyor)

// Standart C++ kütüphaneleri
#include <cmath>          // Sinüs, kosinüs, karekök gibi matematiksel işlemler için
#include <cstdio>         // Konsola veya dosyaya log yazdırmak için
#include <string>         // Metin işlemleri
#include <unordered_set>  // Hızlı arama yapılabilen veri yapıları (Unique listeler)
#include <unordered_map>  // Key-Value (Anahtar-Değer) eşleşmesi tutan veri yapısı (Dictionary gibi)
#include <mutex>          // Multi-threading (Çoklu işlem) sırasında verilerin çakışmasını önleyen kilit mekanizması

namespace student::charanim {

// Karakterin içinde bulunabileceği durumlar (İsimler yeni konsepte göre düzenlendi)
enum class MotionMode { Idle = 0, Walk = 1, Run = 2, Jump = 3, Crawl = 4, Clap = 5, Wave = 6, SitCrossed = 7, Kick = 8, Kneel = 9, Swim = 10 };

// Her bir eklemin X (Pitch), Y (Yaw), Z (Roll) eksenlerindeki açılarını tutan yapı
struct SmoothedJoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Karakterin dünyadaki fiziksel ve animasyon durumunu anlık olarak tutan asıl "Model" verimiz
struct EntityState {
    bool hasPos = false; // Başlangıç pozisyonu alındı mı?
    double lat = 0.0;    // Enlem (Latitude)
    double lon = 0.0;    // Boylam (Longitude)
    double alt = 0.0;    // İrtifa/Yükseklik (Altitude)

    MotionMode currentMode = MotionMode::Idle; // Varsayılan mod (Durma)
    double speedBodyX = 0.0; // Karakterin kendi ekseninde ileri/geri hızı
    double speedBodyY = 0.0; // Karakterin kendi ekseninde sağa/sola hızı
    double speedScale = 0.0; // Toplam hareket şiddeti (Animasyon döngüsünü hızlandırmak için)

    double lastTime = -1.0;  // Bir önceki karenin zamanı (Delta time hesaplamak için)
    std::unordered_map<std::string, SmoothedJoint> joints; // Tüm eklemlerin güncel açıları
};

// Asenkron çalışan Navigasyon ve Animasyon thread'lerinin (iş parçacıklarının) birbirini ezmesini önleyen kilit
static std::mutex g_stateMutex;
// Oyundaki karakterlerin (ID'lerine göre) state'lerini tutan global hafıza
static std::unordered_map<std::string, EntityState> g_entityStates;
static bool g_dumpedJoints = false; // Loglama yapılıp yapılmadığını kontrol eden bayrak

// KLAVYE DİNLEME VE HIZ HESAPLAMA FONKSİYONU
static void UpdateKeyboardState(EntityState& state) {
    // Tuş atamaları 1'den 0'a kadar sıralı ve mantıklı hale getirildi
    bool k1 = (GetAsyncKeyState('1') & 0x8000) != 0; // Walk
    bool k2 = (GetAsyncKeyState('2') & 0x8000) != 0; // Run
    bool k3 = (GetAsyncKeyState('3') & 0x8000) != 0; // Jump
    bool k4 = (GetAsyncKeyState('4') & 0x8000) != 0; // Crawl 
    bool k5 = (GetAsyncKeyState('5') & 0x8000) != 0; // Clap 
    bool k6 = (GetAsyncKeyState('6') & 0x8000) != 0; // Wave
    bool k7 = (GetAsyncKeyState('7') & 0x8000) != 0; // SitCrossed
    bool k8 = (GetAsyncKeyState('8') & 0x8000) != 0; // Kick
    bool k9 = (GetAsyncKeyState('9') & 0x8000) != 0; // Kneel
    bool k0 = (GetAsyncKeyState('0') & 0x8000) != 0; // Swim

    double inputX = 0.0; // İleri/Geri
    double inputY = 0.0; // Sağ/Sol

    if ((GetAsyncKeyState('K') & 0x8000) != 0) inputX += 1.0;
    if ((GetAsyncKeyState('J') & 0x8000) != 0) inputX -= 1.0;
    if ((GetAsyncKeyState('L') & 0x8000) != 0) inputY += 1.0;
    if ((GetAsyncKeyState('H') & 0x8000) != 0) inputY -= 1.0;

    // 1-2-3-4 tuşlarına basılı tutulduğunda HJKL gibi otomatik ileri gitme etkisi
    if (k1 || k2 || k3 || k4) {
        inputX += 1.0;
    }

    // Basılı tutulan tuşa göre modu belirle
    if (k4) state.currentMode = MotionMode::Crawl;
    else if (k3) state.currentMode = MotionMode::Jump;
    else if (k2) state.currentMode = MotionMode::Run;
    else if (k1) state.currentMode = MotionMode::Walk;
    else if (k0) state.currentMode = MotionMode::Swim;
    else if (k9) state.currentMode = MotionMode::Kneel;
    else if (k8) state.currentMode = MotionMode::Kick;
    else if (k7) state.currentMode = MotionMode::SitCrossed;
    else if (k6) state.currentMode = MotionMode::Wave;        
    else if (k5) state.currentMode = MotionMode::Clap;        
    else if (inputX != 0.0 || inputY != 0.0) {
        // HJKL ile hareket ediliyorsa ama 1-2-3'e basılmıyorsa varsayılan olarak Walk yap
        state.currentMode = MotionMode::Walk;
    } else {
        // HİÇBİR TUŞA BASILMIYORSA ANINDA IDLE
        state.currentMode = MotionMode::Idle;
    }

    double speedLimit = 0.0;
    if (state.currentMode == MotionMode::Walk) speedLimit = 1.0;
    else if (state.currentMode == MotionMode::Jump) speedLimit = 0.5;
    else if (state.currentMode == MotionMode::Run) speedLimit = 2.0;
    else if (state.currentMode == MotionMode::Crawl) speedLimit = 0.3;
    else if (state.currentMode == MotionMode::Clap) speedLimit = 1.0;
    else if (state.currentMode == MotionMode::Wave) speedLimit = 1.0;
    else if (state.currentMode == MotionMode::SitCrossed) speedLimit = 1.0;
    else if (state.currentMode == MotionMode::Kick) speedLimit = 1.0;
    else if (state.currentMode == MotionMode::Kneel) speedLimit = 1.0;
    else if (state.currentMode == MotionMode::Swim) speedLimit = 1.0;

    double length = std::sqrt(inputX * inputX + inputY * inputY);
    if (length > 0.0) {
        inputX /= length;
        inputY /= length;
    }

    state.speedBodyX = inputX * speedLimit;
    state.speedBodyY = inputY * speedLimit;
    state.speedScale = std::sqrt(state.speedBodyX * state.speedBodyX + state.speedBodyY * state.speedBodyY);
}

// KARAKTERİN DÜNYA ÜZERİNDEKİ HAREKETİNİ (NAVİGASYON) KONTROL EDEN SINIF
class StudentNavigationModel final : public arkheon::astsim::IModel, public arkheon::astsim::INavigationModel {
public:
    // Motorun bu sınıfı hangi isimle tanıyacağını belirler
    [[nodiscard]] std::string getTypeName() const override { return "navigationModelLowFidelity"; }
    // Motorun bu sınıftan yeni kopyalar (instance) üretebilmesi için gerekli Clone fonksiyonu
    [[nodiscard]] std::unique_ptr<arkheon::astsim::IModel> clone() const override { return std::make_unique<StudentNavigationModel>(*this); }

    // Her karede (frame) motor tarafından tetiklenen, karakterin yeni konumunu hesaplayan fonksiyon
    [[nodiscard]] bool evaluate(const arkheon::astsim::NavigationModelInput& input, arkheon::astsim::NavigationModelOutput& output) const override {
        double dt = input.deltaTimeSeconds; // Önceki kareden bu yana geçen zaman
        if (dt <= 0.001) dt = 0.02; // Zaman sıfır gelirse programın çökmemesi için varsayılan değer

        // Multi-threading çakışmasını engellemek için hafızayı kilitler
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto& state = g_entityStates[input.entityId];

        // Klavyeden gelen son komutları okuyup hızları günceller
        UpdateKeyboardState(state);

        // Karakterin dünyadaki başlangıç Enlem/Boylam değerini motordan sadece 1 kere okur
        if (!state.hasPos) {
            if (input.transformComponentConfig.has("positionGeodetic")) {
                auto posConfig = input.transformComponentConfig.get("positionGeodetic");
                if (posConfig.isObject()) {
                    state.lat = posConfig.get("latitude").asDouble();
                    state.lon = posConfig.get("longitude").asDouble();
                    state.alt = posConfig.get("altitude").asDouble();
                    state.hasPos = true;
                } else if (posConfig.isArray() && posConfig.size() >= 3) {
                    state.lat = posConfig.at(0).asDouble();
                    state.lon = posConfig.at(1).asDouble();
                    state.alt = posConfig.at(2).asDouble();
                    state.hasPos = true;
                }
            }
        }

        // Karakterin dünyada hangi yöne baktığını (Quaternion - q_w, q_x, q_y, q_z) okur
        double q_w = 1.0, q_x = 0.0, q_y = 0.0, q_z = 0.0;
        if (input.transformComponentConfig.has("orientationBodyToNedQuat")) {
            auto qConfig = input.transformComponentConfig.get("orientationBodyToNedQuat");
            if (qConfig.isObject()) {
                q_w = qConfig.get("w").asDouble();
                q_x = qConfig.get("x").asDouble();
                q_y = qConfig.get("y").asDouble();
                q_z = qConfig.get("z").asDouble();
            } else if (qConfig.isArray() && qConfig.size() >= 4) {
                q_w = qConfig.at(0).asDouble();
                q_x = qConfig.at(1).asDouble();
                q_y = qConfig.at(2).asDouble();
                q_z = qConfig.at(3).asDouble();
            }
        }

        // --- MATRİS DÖNÜŞÜMÜ (Body Frame -> NED Frame) ---
        // Karakterin kendi eksenindeki hızını, dünyanın Kuzey(X)-Doğu(Y)-Aşağı(Z) eksenine çevirir.
        double bx_x = 1.0 - 2.0 * (q_y * q_y + q_z * q_z);
        double bx_y = 2.0 * (q_x * q_y + q_w * q_z);
        double bx_z = 2.0 * (q_x * q_z - q_w * q_y);
        
        double by_x = 2.0 * (q_x * q_y - q_w * q_z);
        double by_y = 1.0 - 2.0 * (q_x * q_x + q_z * q_z);
        double by_z = 2.0 * (q_y * q_z + q_w * q_x);

        // Nihai dünya hız vektörleri (vx = Kuzey, vy = Doğu, vz = Aşağı)
        double vx = state.speedBodyX * bx_x + state.speedBodyY * by_x;
        double vy = state.speedBodyX * bx_y + state.speedBodyY * by_y;
        double vz = state.speedBodyX * bx_z + state.speedBodyY * by_z;

        // Hesaplanan hızları JSON formatında oyun motoruna geri gönderir
        auto velArray = arkheon::astlib::JsonValue::array();
        static_cast<void>(velArray.pushBack(arkheon::astlib::JsonValue::fromDouble(vx)));
        static_cast<void>(velArray.pushBack(arkheon::astlib::JsonValue::fromDouble(vy)));
        static_cast<void>(velArray.pushBack(arkheon::astlib::JsonValue::fromDouble(vz)));

        output.hasState = true;
        static_cast<void>(output.state.set("velocityNed", velArray));

        // Eğer başlangıç pozisyonu alındıysa, hızı (m/s) enlem ve boylama ekler
        if (state.hasPos) {
            double meterToLat = 1.0 / 111139.0; // 1 derecelik enlem yaklaşık 111 kilometredir
            double latRad = state.lat * 3.141592653589793 / 180.0; // Dereceyi radyana çevir
            double meterToLon = 1.0 / (111139.0 * std::cos(latRad)); // Dünyanın eğriliğine göre boylam hesaplaması

            // Yeni pozisyon (Konum = Eski Konum + Hız * Zaman)
            state.lat += vx * dt * meterToLat;
            state.lon += vy * dt * meterToLon;

            // Yeni pozisyonu JSON formatında motora gönderir
            auto newPos = arkheon::astlib::JsonValue::object();
            static_cast<void>(newPos.set("latitude", arkheon::astlib::JsonValue::fromDouble(state.lat)));
            static_cast<void>(newPos.set("longitude", arkheon::astlib::JsonValue::fromDouble(state.lon)));
            static_cast<void>(newPos.set("altitude", arkheon::astlib::JsonValue::fromDouble(state.alt)));
            static_cast<void>(output.state.set("positionGeodetic", newPos));
        }

        return true; // İşlem başarılı
    }
};

namespace {

// KARAKTERİN ANİMASYONLARINI VE KEMİK AÇILARINI HESAPLAYAN FONKSİYON
[[nodiscard]] bool evaluateStudentAnimation(
    const arkheon::astsim::AnimationModelInput& input,
    arkheon::astsim::AnimationModelOutput& output) {
    
    const double t = input.simulationTimeSeconds; // Simülasyonun o anki zamanı

    MotionMode mode = MotionMode::Idle;
    double speedScale = 0.0;

    double dt = 0.02;
    double currentPhase = 0.0; // Işınlanmayı çözecek asıl değer

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto& state = g_entityStates[input.entity.entityId];
        UpdateKeyboardState(state);
        mode = state.currentMode;
        speedScale = state.speedScale;

        // Işınlanmayı (Teleport) çözen asıl hamle: Zaman farkı (dt) ile Faz Birikimi
        if (state.lastTime > 0.0) {
            dt = t - state.lastTime;
            dt = std::min(dt, 0.1); // İlk açılışta devasa zıplamayı önler
        }
        
        // Hangi moddaysak döngü frekansını (hızını) belirliyoruz
        double freq = 0.0;
        if (mode == MotionMode::Walk) freq = 4.0;
        else if (mode == MotionMode::Run) freq = 6.5;
        else if (mode == MotionMode::Clap) freq = 6.0;      
        else if (mode == MotionMode::Wave) freq = 2.0;      
        else if (mode == MotionMode::SitCrossed) freq = 8.5; 
        else if (mode == MotionMode::Kick) freq = 6.0;
        else if (mode == MotionMode::Kneel) freq = 4.0;
        else if (mode == MotionMode::Swim) freq = 10.0;
        
        // Fazı sürekli olarak üzerine ekleyerek biriktiriyoruz. 
        double effectiveScale = (speedScale > 0.0) ? speedScale : 1.0;
        state.joints["phase_acc"].x += dt * freq * effectiveScale;

        currentPhase = state.joints["phase_acc"].x;
    }

    // Motorun var olan diğer animasyonlarını tamamen temizler (Kontrolü biz alıyoruz)
    output.clearExistingJointOverrides = true;
    output.jointOverrides.clear();

    // Bu karede hesaplanacak hedef kemik açıları
    std::unordered_map<std::string, SmoothedJoint> targets;

    // --- IDLE (BEKLEME) DURUMU ---
    // Kolları doğal bir şekilde aşağıya sarkıtır
    double breathPhase = t * 2.0; // Yavaş, ritmik nefes döngüsü
    double breathSwing = std::sin(breathPhase) * 0.05; // Çok hafif (5%) salınım şiddeti

    // Omuzlar nefes alırken anatomik olarak çok hafif yukarı-aşağı esner
    targets["leftShoulder"] = {0.25 + breathSwing, 1.47, 0.35};
    targets["rightShoulder"] = {0.25 + breathSwing, 1.47, 0.35};

    // Nefes alırken göğüs şiştiği için dirsekler çok hafif dışa açılır
    targets["leftElbow"] = {0.0, 0.0, -0.31 + (breathSwing * 0.5)};
    targets["rightElbow"] = {0.0, 0.0, -0.31 - (breathSwing * 0.5)};

    targets["leftHip"] = {0.0, 0.0, 0.1 - (breathSwing * 0.35)};
    targets["rightHip"] = {0.0, 0.0, -0.1 + (breathSwing * 0.35)};
    // Karakterin ağırlık merkezi nefesle birlikte milimetrik hareket eder
    targets["leftKnee"] = {0.0, 0.0, 0.01 - (breathSwing * 0.01)};
    targets["rightKnee"] = {0.0, 0.0, -0.01 + (breathSwing * 0.01)};
    targets["leftAnkle"] = {0.0, 0.0, 0.0};
    targets["rightAnkle"] = {0.0, 0.0, 0.0};

    // --- YÜRÜME (WALK) DURUMU ---
    if (mode == MotionMode::Walk && speedScale > 0.0) {
        // Zaman ve hıza bağlı bir döngü oluşturur (Faz)
        double cycle = currentPhase;
        // Sol ve sağ bacak için zıt işaretli sinüs dalgaları üretir (Biri öne giderken diğeri arkaya gider)
        double lSwing = std::sin(cycle);
        double rSwing = std::sin(cycle + 3.14159);

        // Kalçayı (Hip) Z ekseninde (ileri-geri) dalgaya göre hareket ettirir
        targets["leftHip"].z = lSwing * 0.6;
        targets["rightHip"].z = rSwing * 0.6;

        targets["rightShoulder"].z = rSwing;
        targets["rightShoulder"].x = rSwing * 0.5;
        targets["rightElbow"].z = rSwing * 0.2;
        targets["rightElbow"].y = rSwing * 0.02;

        targets["leftShoulder"].z = lSwing;
        targets["leftShoulder"].x = lSwing * 0.5;
        targets["leftElbow"].z = lSwing * 0.2;
        targets["leftElbow"].y = lSwing * 0.02;

        // Sadece bacak ileri atılırken (lSwing > 0) dizin bükülmesini sağlar, yere basarken düz tutar
        double lKnee = (lSwing > 0.0) ? lSwing * -0.25 : -0.45;
        double rKnee = (rSwing > 0.0) ? rSwing * -0.25 : -0.45;

        targets["leftKnee"].z = lKnee;
        targets["rightKnee"].z = rKnee;

        // Ayak bileğini diz bükülmesine orantılı olarak yere paralel tutmak için ters büker
        targets["leftAnkle"].z = -lKnee * 0.3;
        targets["rightAnkle"].z = -rKnee * 0.3;
    }

    // --- KOŞMA (RUN) DURUMU ---
    if (mode == MotionMode::Run && speedScale > 0.0) {
        // Döngü hızını yürümeye göre artırdık (4.0 yerine 6.5)
        double cycle = currentPhase;
        
        double lSwing = std::sin(cycle);
        double rSwing = std::sin(cycle + 3.14159);

        targets["leftHip"].z = lSwing * 0.7;
        targets["rightHip"].z = rSwing * 1;

        double armX = 0.25; 
        
        targets["leftShoulder"] = {0.1 * lSwing , 1.0,1.5 * lSwing}; 
        targets["leftElbow"] = {0.0, 0.0, -1.4 + (lSwing * 0.8)};

        targets["rightShoulder"] = {0.1 * rSwing , 1.0,1.5 * rSwing};
        targets["rightElbow"] = {0.0, 0.0, -1.4 + (rSwing * 0.4)};

        double lKnee = (lSwing > 0.0) ? lSwing * -0.25 : -1.5;
        double rKnee = (rSwing > 0.0) ? rSwing * -0.25 : -1.5;

        targets["leftKnee"].z = lKnee;
        targets["rightKnee"].z = rKnee;

        targets["leftAnkle"].z = lKnee * 0.3;
        targets["rightAnkle"].z = rKnee * 0.3;
    }

    // --- JUMP (ZIPLAMA) DURUMU ---
    if (mode == MotionMode::Jump && speedScale > 0.0) {
        targets["leftShoulder"] = {-0.2, 0.5, 0.2};
        targets["rightShoulder"] = {-0.2, 0.5, 0.2};
        targets["leftElbow"] = {0.6, 0.2, -0.6};
        targets["rightElbow"] = {0.6, 0.2, -0.6};

        targets["leftAnkle"] = {0.2, 0, 0.8};
        targets["rightAnkle"] = {0.2, 0, 0.8};

        targets["leftHip"].z = 1.2;
        targets["rightHip"].z = 1.2;
        targets["leftKnee"].z = -2.5;
        targets["rightKnee"].z = -2.5;
    }

    // --- SÜRÜNME (CRAWL) DURUMU ---
    if (mode == MotionMode::Crawl && speedScale > 0.0) {
        double cycle = t * 8.0 * speedScale;
        double lSwing = std::sin(cycle);
        double rSwing = std::sin(cycle + 3.14159);

        targets["leftHip"].z = -0.15 + (lSwing * 0.34); 
        targets["rightHip"].z = -0.1 + (rSwing * 0.34);
        
        targets["leftKnee"].z = -1.4 + (lSwing > 0.0 ? lSwing * -0.5 : 0.0);
        targets["rightKnee"].z = -1.4 + (rSwing > 0.0 ? rSwing * -0.5 : 0.0);

        targets["leftAnkle"].z = -1;
        targets["rightAnkle"].z = -1;

        targets["leftShoulder"].z = lSwing * 0.25;
        targets["leftShoulder"].y = 1.2 + lSwing * 0.05;
        targets["leftShoulder"].x = lSwing * 0.25;

        targets["rightShoulder"].z = rSwing * 0.35;
        targets["rightShoulder"].y = 1.2 + rSwing * 0.05;
        targets["rightShoulder"].x = rSwing * 0.35;

        targets["rightElbow"].y = -0.3;
        targets["rightElbow"].z = -1.25 + rSwing * 0.2 ;

        targets["leftElbow"].y = -0.3;
        targets["leftElbow"].z = -1.25 + lSwing * 0.2 ;
    }

    // YENİ ANİMASYONLAR BAŞLANGIÇ --------------------------------------------------------------------------------------------------------

    // --- 5: CLAP (ALKIŞLAMA) DURUMU ---
    if (mode == MotionMode::Clap) {
        double clapPhase = currentPhase * 5.0; 
        double clapMotion = std::sin(clapPhase) * 0.50; 
        double bounce = std::sin(clapPhase * 0.5) * 0.03; // Ritmik diz yaylanması

        targets["leftHip"] = {0.0, 0.0, 0.1};
        targets["rightHip"] = {0.0, 0.0, 0.1};
        targets["leftKnee"] = {0.0, 0.0, -0.1 + bounce * 0.1};
        targets["rightKnee"] = {0.0, 0.0, -0.1 - bounce * 0.1};
        targets["leftAnkle"] = {0.0, 0.0, 0.0};
        targets["rightAnkle"] = {0.0, 0.0, 0.0};

        targets["leftShoulder"] = {1.25 + clapMotion, 1.47, 0.8}; 
        targets["leftElbow"] = {0.0, 0.0, -2.1}; 

        targets["rightShoulder"] = {1.25 + clapMotion, 1.47, 0.8}; 
        targets["rightElbow"] = {0.0, 0.0, -2.1}; 
    }

    // --- 6: WAVE (EL SALLAMA) DURUMU ---
    if (mode == MotionMode::Wave) {
        double wavePhase = currentPhase * 2.0; 
        double waveSwing = std::sin(wavePhase) * 0.4; 
        
        double bodySway = std::sin(currentPhase * 1.0) * 0.05;

        targets["leftHip"] = {0.0, 0.0, 0.0 + bodySway * 0.3};
        targets["rightHip"] = {0.0, 0.0, 0.0 - bodySway * 0.3};
        targets["leftKnee"] = {0.0, 0.0, 0.0};
        targets["rightKnee"] = {0.0, 0.0, 0.0};
        targets["leftAnkle"] = {0.0, 0.0, 0.0};
        targets["rightAnkle"] = {0.0, 0.0, 0.0};

        targets["leftShoulder"] = {0.2, 1.47 + bodySway, 0.0 + bodySway * 1.2}; 
        targets["leftElbow"] = {0.0, 0.0, -0.1}; 

        targets["rightShoulder"] = {-1.0, 0.47 + bodySway * 1.5, 0.0 + bodySway}; 
        targets["rightElbow"] = {0.0, 0.0 , -2.0 + waveSwing}; 
    }

    // --- 7: SIT CROSSED (GÖRÜNMEZ SANDALYEDE BACAK BACAK ÜSTÜNE ATMA) ---
    if (mode == MotionMode::SitCrossed) { 
        // Enhance: Doğal sallanma ve nefes efekti eklendi
        double sway = std::sin(currentPhase * 0.5) * 0.04;
        double breath = std::sin(currentPhase * 0.5) * 0.02;

        targets["leftHip"] = {0.0, 0.0, 1.5};       
        targets["leftKnee"] = {0.0, 0.0, -1.5};     
        targets["leftAnkle"] = {0.0, 0.0, 0.0};     

        targets["rightHip"] = {1.45, 0.0, 1.8};     
        targets["rightKnee"] = {0.0, 0.0, -1.5};    
        targets["rightAnkle"] = {breath * 3.4 , 0.0, -0.2 - breath * 3.4 };   

        targets["leftShoulder"] = {-0.4, 1.5, 0.3};
        targets["rightShoulder"] = {-0.4, 1.5, 0.3}; 

        targets["leftElbow"] = {1.85, 1.0, -0.8};
        targets["rightElbow"] = {2.7, 1.0 , -1.0}; 
    }

        // --- 8:  KICK (TEKME) DURUMU ---
    if (mode == MotionMode::Kick) {
        double kick = std::sin(currentPhase); 

        targets["leftHip"] = {0.0, 0.0, 0.1 + kick * 0.01};
        targets["leftKnee"] = {0.0, 0.0, -0.2}; 
        targets["leftAnkle"] = {0.0, 0.0, 0.0};
      
        targets["rightHip"] = {0.0, 0.0, (kick > 0.5) ? 2.0 : 0.5}; 
        targets["rightKnee"] = {0.0, 0.0, (kick > 0.5) ? 0.0 : -2.2}; 
        targets["rightAnkle"] = {0.0, 0.0, -0.6}; 

        targets["leftShoulder"] = {kick * 0.1, 1.0, 0.8 + kick * 0.5}; 
        targets["rightShoulder"] = {kick * 0.1, 1.0, 0.8 + kick * 0.5};
        targets["leftElbow"] = {0.0, kick * 0.1, -0.65 + kick * 0.2};
        targets["rightElbow"] = {0.0, kick * 0.1, -0.65 + kick * 0.2};
    }

    // --- 9: KNEEL (DİZ ÜSTÜNDE OTURMA) DURUMU  ---
    if (mode == MotionMode::Kneel) {
        
        targets["leftHip"] = {0.2, 0.0, 1.65};
        targets["leftKnee"] = {0.0, 0.0, -2.72};
        targets["leftAnkle"] = {0.0, 0.0, -1.5};

        targets["rightHip"] = {0.2, 0.0, 1.65};
        targets["rightKnee"] = {0.0, 0.0, -2.72}; // Diz dümdüz
        targets["rightAnkle"] = {0.0, 0.0, -1.5};

        targets["leftShoulder"] = {-0.4, 1.5, 0.3};
        targets["rightShoulder"] = {-0.4, 1.5, 0.3}; 

        targets["leftElbow"] = {1.85, 1.0, -0.8};
        targets["rightElbow"] = {1.85, 1.0 , -0.8}; 
    }

    // --- 0: SWIM (YÜZME) DURUMU ---
    if (mode == MotionMode::Swim) {
        double punchPhase = std::sin(currentPhase);
        double dip = (punchPhase < 0.0) ? -punchPhase * 0.4 : 0.0; 
        double explode = (punchPhase > 0.0) ? punchPhase : 0.0;    

        targets["leftHip"] = {0.0, 0.0, -0.9 + (punchPhase * 0.5)};
        targets["rightHip"] = {0.0, 0.0, -0.9 - (punchPhase * 0.5)};
        targets["leftKnee"] = {0.0, 0.0, -0.2 + (punchPhase * 0.4)};
        targets["rightKnee"] = {0.0, 0.0, -0.1 - (punchPhase * 0.4)};

        
        targets["leftShoulder"] = {1.8, 0.3, -1 + (explode * 2.0)};
        targets["rightShoulder"] = {1.8, 0.3, -1 + (explode * 1.8)};
     

        targets["rightElbow"].z = punchPhase * 0.1;
        targets["rightElbow"].y = -1.25 + (punchPhase * 0.7);

        targets["leftElbow"].z = punchPhase * 0.1;
        targets["leftElbow"].y = -1.25 + (punchPhase * 0.5);

        targets["leftAnkle"].z = punchPhase * -0.3;
        targets["rightAnkle"].z = punchPhase * 0.3;
    }   
    
    // YENİ ANİMASYONLAR BİTİŞ --------------------------------------------------------------------------------------------------------
    
    // --- EXPONENTIAL DECAY (PÜRÜZSÜZLEŞTİRME) DÖNGÜSÜ ---
    // Animasyonlar (örn: Koşarken aniden durma) arası sert atlamaları engeller.
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto& state = g_entityStates[input.entity.entityId];
        
        // İlk frame ise (animasyon yeni başlıyorsa) değerleri direkt atar
        if (state.lastTime < 0.0 || t < state.lastTime) {
            for (const auto& kv : targets) {
                state.joints[kv.first] = kv.second;
            }
        } else {
            // Asimptotik matematik: Mevcut açı ile Hedef açı arasındaki farkı, zamana bağlı bir Alpha çarpanı ile kapatır.
            double dt = t - state.lastTime;
            if (dt < 0.0) dt = 0.0;

            double smoothFactor = 12.0;

            if (mode == MotionMode::Jump) {
                smoothFactor = 4.0;  // Çooook smooth (Sinematik yavaş çöküş ve kalkış)
            } 
            else if (mode == MotionMode::Run) {
                smoothFactor = 20.0; // Az smoothing (Koşuya geçerken atik ve keskin bir tepki)
            }
            else if (mode == MotionMode::Crawl) {
                smoothFactor = 6.0; // Sürünme pozisyonuna pürüzsüz ama kararlı geçiş
            }
            else if (mode == MotionMode::Kick || mode == MotionMode::Kneel || mode == MotionMode::Swim) {
                smoothFactor = 25.0; // Vuruşlar keskin ve atik olmalı
            }
            else if (mode == MotionMode::Wave || mode == MotionMode::Clap) {
                smoothFactor = 10.0; // Animasyonlara dönüş doğal olmalı
            }
            else if (mode == MotionMode::SitCrossed) {
                smoothFactor = 25.0; // Sandalyeye oturur gibi daha ağır geçiş
            }
            
            dt = std::clamp(dt, 0.001, 0.1);
            double alpha = 1.0 - std::exp(-smoothFactor * dt);

            for (const auto& kv : targets) {
                auto& smoothed = state.joints[kv.first];
                // Yeni Değer = Eski Değer + (Hedef - Eski Değer) * Alpha
                smoothed.x += (kv.second.x - smoothed.x) * alpha;
                smoothed.y += (kv.second.y - smoothed.y) * alpha;
                smoothed.z += (kv.second.z - smoothed.z) * alpha;
            }
        }
        state.lastTime = t;

        // Yumuşatılmış (smoothed) son açı değerlerini JSON listesine çevirip motora iletir
        for (const auto& j : input.entity.joints) {
            if (state.joints.count(j.jointId)) {
                const auto& val = state.joints[j.jointId];
                output.jointOverrides.push_back({j.jointId, val.x, val.y, val.z});
            }
        }
    }

    return !output.jointOverrides.empty(); // Eğer override edilecek kemik varsa True döner
}
} // namespace

// BUNDAN SONRAKİ KISIMLAR N8RO MOTORUNUN C++ EKLENTİMİZİ (DLL) TANIMASI İÇİN GEREKLİ OLAN STANDART KAYIT FONKSİYONLARIDIR

// Eklenti arayüz versiyonunu döner
int StudentCharAnimPlugin::getInterfaceVersion() const { return 1; }

// Eklentinin adını, versiyonunu ve yazarını motora bildirir
arkheon::astlib::PluginMetadata StudentCharAnimPlugin::getMetadata() const {
    arkheon::astlib::PluginMetadata metadata;
    metadata.setPluginId("student-char-anim");
    metadata.setVersion("3.0.0");
    metadata.setAuthor("Student");
    return metadata;
}

// Oyun motoru açıldığında çalışan ilk fonksiyon. Sınıflarımızı motora tanıtır.
void StudentCharAnimPlugin::initialize(arkheon::astlib::PluginContext& context) {
    initialized_ = true;
    shutdown_ = false;

    modelType_ = "animationModelNathanHuman"; // Bizim yöneteceğimiz insan modelinin ID'si
    modelFactoryRegistry_ = nullptr;

    // Motorun fabrika (Factory) servisini arar ve alır
    if (context.services) {
        auto* rawService = context.services->getService(arkheon::astsim::IModelPluginService::kPluginServiceId);
        auto* service = static_cast<arkheon::astsim::IModelPluginService*>(rawService);
        modelFactoryRegistry_ = service ? &service->modelFactoryRegistry() : nullptr;
    }

    if (modelFactoryRegistry_) {
        // Yazdığımız navigasyon sınıfını, motorun default sistemlerini ezecek şekilde kaydeder
        modelFactoryRegistry_->registerFactory("navigationModelLowFidelity", std::make_unique<StudentNavigationModel>());
        modelFactoryRegistry_->registerFactory("navigationModelStudent", std::make_unique<StudentNavigationModel>());
        modelFactoryRegistry_->registerFactory("navigationModelNathanHuman", std::make_unique<StudentNavigationModel>());
        modelFactoryRegistry_->registerFactory("navigationModelHighFidelity", std::make_unique<StudentNavigationModel>());

        // Yazdığımız animasyon fonksiyonunu, var olan animasyonların yerine çalışması için kaydeder
        auto* prototypeBase = modelFactoryRegistry_->getRegisteredPrototype(modelType_);
        auto* prototypeAnimationModel = dynamic_cast<arkheon::astsim::IAnimationModel*>(prototypeBase);

        if (prototypeAnimationModel) {
            // Ezeceğimiz N8RO animasyonlarının tam listesi
            const char* allCodes[] = {
                "Idle", "Walk", "Idle Alert", "Idle Shake", "Idle Stopped",
                "Walk Forward", "Run", "Idle Breathing", "Idle Neutral",
                "Student Walk", "Student Motion", "T-Pose", ""
            };

            // Listede dönüp hepsine kendi fonksiyonumuzu (evaluateStudentAnimation) atar
            for (const char* code : allCodes) {
                if (prototypeAnimationModel->registerAnimation(code, evaluateStudentAnimation)) {
                    registeredCodes_.push_back(code);
                }
            }
        }
    }
}

// Simülasyon devam ettiği sürece her karede (frame) çağrılır (Bu plugin'de içi boş bırakılmış)
void StudentCharAnimPlugin::tick(double dt) {
    static_cast<void>(dt);
}

// Motor kapanırken çalışan temizlik (Garbage Collection) fonksiyonu
void StudentCharAnimPlugin::shutdown() {
    // Motora kaydettiğimiz tüm sınıfları ve eklentileri hafızadan (RAM) silerek iade eder
    if (modelFactoryRegistry_) {
        auto* prototypeBase = modelFactoryRegistry_->getRegisteredPrototype(modelType_);
        auto* prototypeAnimationModel = dynamic_cast<arkheon::astsim::IAnimationModel*>(prototypeBase);
        if (prototypeAnimationModel) {
            for (const auto& code : registeredCodes_) {
                static_cast<void>(prototypeAnimationModel->registerAnimation(code, arkheon::astsim::IAnimationModel::AnimationEvaluationFunction{}));
            }
        }
    }
    registeredCodes_.clear();
    shutdown_ = true;
    modelFactoryRegistry_ = nullptr;
}

} // namespace student::charanim

// C TABANLI İHRACATLAR (EXPORTS)
// Motor, DLL dosyamızı yüklerken doğrudan bu C fonksiyonlarını arar. C++ class'larına buradan ulaşır.
extern "C" {
// Eklentiden yeni bir kopya oluşturur
__declspec(dllexport) arkheon::astlib::IPlugin* create_plugin() {
    return new student::charanim::StudentCharAnimPlugin();
}
// Eklentiyi hafızadan yok eder (Memory leak önler)
__declspec(dllexport) void destroy_plugin(arkheon::astlib::IPlugin* plugin) {
    if (plugin) delete plugin;
}
// Motorun bu DLL'in doğru bir eklenti olduğunu anlaması için gereken imza (Şifre)
__declspec(dllexport) const char* get_plugin_signature() {
    return "ARKHEON_PLUGIN_V1";
}
}