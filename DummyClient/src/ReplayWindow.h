#pragma once
#include "Replay.h"

// 서버/DB 없이 검증된 기록을 표시하는 Win32 2D 뷰어다.
void RunReplayWindow(const Replay::stArchive& archive,
                     const fs::path& imageFile = {},
                     double imageSeconds = 0);

#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>

struct stReplayAttackLine
{
    double x = 0;
    double y = 0;
    double targetX = 0;
    double targetY = 0;
    double until = 0;
    wire::Race race = wire::RACE_UNSPECIFIED;
};

// 재생/입력/그리기 함수의 선언. 본문은 ReplayWindow.cpp에 있다.
class CReplayWindow
{
public:
    explicit CReplayWindow(const Replay::stArchive& archive);
    void Run();
    void SaveImage(const fs::path& path, double seconds);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);
    void ReadMap();
    void LoadImages();
    void CreateControls();
    void Layout();
    void LayoutMap();
    void Update();
    void Refresh();
    void Seek(double milliseconds);
    void SetSpeed();
    void AddEvents(const wire::ReplayFrame& frame, const wire::WorldState& before);
    void Paint(HDC dc);
    void Draw(Gdiplus::Graphics& graphics);
    void DrawMap(Gdiplus::Graphics& graphics);
    void DrawPanel(Gdiplus::Graphics& graphics);
    void Portrait(Gdiplus::Graphics& graphics, const wire::UnitState& unit, float x, float y, float size);
    Gdiplus::PointF Position(double x, double y) const;
    const wire::UnitState* Selected() const;

    const Replay::stArchive& m_archive;
    wire::WorldState m_state, m_initial;
    HWND m_window = nullptr;
    array<HWND, 7> m_controls{};
    unordered_map<string, unique_ptr<Gdiplus::Image>> m_images;
    vector<stReplayAttackLine> m_attacks;
    deque<string> m_events;
    string m_selected;
    string m_failure;
    size_t m_cursor = 0;
    bool m_playing = false;
    bool m_dragging = false;
    double m_speed = 1;
    double m_playhead = 0;
    double m_minX = 0;
    double m_maxX = 0;
    double m_minY = 0;
    double m_maxY = 0;
    float m_width = 1180, m_height = 820, m_scale = 1, m_mapX = 0, m_mapY = 0, m_mapWidth = 0,
          m_mapHeight = 0;
    chrono::steady_clock::time_point m_updated = chrono::steady_clock::now();
};
