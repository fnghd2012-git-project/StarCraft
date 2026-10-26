#include "ReplayWindow.h"
#include "Config.h"
#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>

namespace
{
namespace Drawing = Gdiplus;
constexpr int PlayButton = 101, PauseButton = 102, SpeedBox = 103, SpeedButton = 104;
constexpr int Timeline = 105, BattleButton = 106, StatusLabel = 107;
const Drawing::Color Background(17, 23, 30), Panel(26, 34, 44), Text(227, 234, 241), Muted(148, 165, 181);

wstring Wide(const string& text)
{
    if (text.empty())
    {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    wstring result(length, L' ');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

string Number(double value, int digits = 1)
{
    ostringstream text;
    text << fixed << setprecision(digits) << value;
    return text.str();
}

Drawing::Color RaceColor(wire::Race race)
{
    if (race == wire::RACE_TERRAN)
    {
        return Drawing::Color(85, 178, 255);
    }
    if (race == wire::RACE_PROTOSS)
    {
        return Drawing::Color(246, 200, 89);
    }
    return Drawing::Color(196, 129, 244);
}

string RaceName(wire::Race race)
{
    if (race == wire::RACE_TERRAN)
    {
        return "테란";
    }
    if (race == wire::RACE_PROTOSS)
    {
        return "프로토스";
    }
    if (race == wire::RACE_ZERG)
    {
        return "저그";
    }
    return "미정";
}

// 모든 그리기 객체는 호출 범위 안에서 해제한다. 이미지 캐시만 창이 닫힐 때까지 유지한다.
void Label(Drawing::Graphics& graphics,
           const string& text,
           float x,
           float y,
           float size = 14,
           Drawing::Color color = Text,
           float width = 340)
{
    Drawing::Font font(L"Malgun Gothic", size, Drawing::FontStyleRegular, Drawing::UnitPixel);
    Drawing::SolidBrush brush(color);
    Drawing::StringFormat format;
    format.SetFormatFlags(Drawing::StringFormatFlagsNoWrap);
    format.SetTrimming(Drawing::StringTrimmingEllipsisCharacter);
    const auto wide = Wide(text);
    graphics.DrawString(wide.c_str(), -1, &font, Drawing::RectF(x, y, width, size + 10), &format, &brush);
}

void Bar(
    Drawing::Graphics& graphics, float x, float y, float width, int value, int maximum, Drawing::Color color)
{
    Drawing::SolidBrush back(Drawing::Color(48, 60, 71)), front(color);
    graphics.FillRectangle(&back, x, y, width, 5.0f);
    const float ratio = maximum > 0 ? clamp(static_cast<float>(value) / maximum, 0.0f, 1.0f) : 0;
    graphics.FillRectangle(&front, x, y, width * ratio, 5.0f);
}

} // namespace

CReplayWindow::CReplayWindow(const Replay::stArchive& archive)
    : m_archive(archive), m_state(archive.StateAt(0)), m_initial(m_state)
{
    m_playhead = static_cast<double>(m_archive.frames.front().elapsed_ms());
    ReadMap();
    LoadImages();
}

void CReplayWindow::ReadMap()
{
    // 현재 Data/Map.ini가 바뀌어도 기록 당시 좌표계를 유지한다. 파일을 디스크로 풀지 않는다.
    unordered_map<string, double> bounds;
    for (const auto& file : m_archive.header.data_files())
    {
        if (file.name() != "Map.ini")
        {
            continue;
        }
        istringstream input(file.content());
        string line, section;
        while (getline(input, line))
        {
            const auto first = line.find_first_not_of(" \t\r\n");
            if (first == string::npos)
            {
                continue;
            }
            line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);
            if (line.front() == '[')
            {
                section = line;
                continue;
            }
            const auto equals = line.find('=');
            if (section != "[Map]" || equals == string::npos)
            {
                continue;
            }
            string key = line.substr(0, equals);
            key.erase(key.find_last_not_of(" \t") + 1);
            if (key != "MinX" && key != "MaxX" && key != "MinY" && key != "MaxY")
            {
                continue;
            }
            const string value = line.substr(equals + 1);
            bounds[key] = ParseNumber(value.substr(value.find_first_not_of(" \t")));
        }
    }
    if (bounds.size() != 4)
    {
        throw runtime_error("Replay Map.ini bounds missing");
    }
    m_minX = bounds.at("MinX");
    m_maxX = bounds.at("MaxX");
    m_minY = bounds.at("MinY");
    m_maxY = bounds.at("MaxY");
    if (m_minX < 0 || m_minY < 0 || m_maxX <= m_minX || m_maxY <= m_minY || m_maxX > 1000000 ||
        m_maxY > 1000000)
    {
        throw runtime_error("Invalid replay map bounds");
    }
}

void CReplayWindow::LoadImages()
{
    // 실행 위치가 어디든 exe 옆 Assets를 사용한다. 없는 이미지는 종족색 원으로 표시한다.
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768)
    {
        throw runtime_error("Cannot locate replay assets directory");
    }
    const fs::path directory = fs::path(executable).parent_path() / "Assets" / "Replay";
    const array<string, 9> names = {
        "Marine", "Ghost", "Medic", "Goliath", "Zealot", "HighTemplar", "Zergling", "Hydralisk", "Defiler"};
    for (size_t i = 0; i < names.size(); ++i)
    {
        const auto path = directory / (names[i] + (i < 4 ? ".jpg" : ".gif"));
        auto up_image = make_unique<Drawing::Image>(path.c_str());
        if (up_image->GetLastStatus() == Drawing::Ok)
        {
            m_images.emplace(names[i], move(up_image));
        }
    }
}

void CReplayWindow::CreateControls()
{
    const array<const wchar_t*, 7> classes = {
        L"BUTTON", L"BUTTON", L"COMBOBOX", L"BUTTON", TRACKBAR_CLASSW, L"BUTTON", L"STATIC"};
    const array<const wchar_t*, 7> captions = {
        L"Play  재생", L"Pause  정지", L"", L"배속 적용", L"", L"첫 전투로", L""};
    for (size_t i = 0; i < m_controls.size(); ++i)
    {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
        if (i == 2)
        {
            style |= CBS_DROPDOWN | WS_VSCROLL;
        }
        if (i == 4)
        {
            style |= TBS_NOTICKS;
        }
        m_controls[i] = CreateWindowExW(0,
                                        classes[i],
                                        captions[i],
                                        style,
                                        0,
                                        0,
                                        100,
                                        30,
                                        m_window,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(PlayButton + i)),
                                        GetModuleHandleW(nullptr),
                                        nullptr);
        if (!m_controls[i])
        {
            throw runtime_error("Cannot create replay control");
        }
        SendMessageW(
            m_controls[i], WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    for (const wchar_t* p_speed : {L"0.25", L"0.5", L"1", L"2", L"4", L"8", L"16"})
    {
        SendMessageW(m_controls[2], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p_speed));
    }
    SetWindowTextW(m_controls[2], L"1");
    SendMessageW(m_controls[4], TBM_SETRANGE, TRUE, MAKELPARAM(0, 10000));
    if (!SetTimer(m_window, 1, 16, nullptr))
    {
        throw runtime_error("Cannot start replay timer");
    }
    Layout();
    Refresh();
}

void CReplayWindow::Layout()
{
    RECT client{};
    GetClientRect(m_window, &client);
    m_width = static_cast<float>(client.right);
    m_height = static_cast<float>(client.bottom);
    LayoutMap();
    const int bottom = client.bottom - 48;
    const array<int, 6> x = {24, 132, 256, 344, 24, 458};
    const array<int, 6> widths = {100, 100, 80, 100, static_cast<int>(max(100L, client.right - 48)), 110};
    for (size_t i = 0; i < 6; ++i)
    {
        MoveWindow(m_controls[i], x[i], i == 4 ? bottom - 40 : bottom, widths[i], i == 2 ? 220 : 30, TRUE);
    }
    MoveWindow(m_controls[6], 590, bottom + 6, max(200L, client.right - 614), 24, TRUE);
    InvalidateRect(m_window, nullptr, FALSE);
}

void CReplayWindow::LayoutMap()
{
    const float availableWidth = max(100.0f, m_width - 390), availableHeight = max(100.0f, m_height - 205);
    m_scale = min(availableWidth / static_cast<float>(m_maxX - m_minX),
                  availableHeight / static_cast<float>(m_maxY - m_minY));
    m_mapWidth = static_cast<float>(m_maxX - m_minX) * m_scale;
    m_mapHeight = static_cast<float>(m_maxY - m_minY) * m_scale;
    m_mapX = 32 + (availableWidth - m_mapWidth) / 2;
    m_mapY = 104 + (availableHeight - m_mapHeight) / 2;
}

Drawing::PointF CReplayWindow::Position(double x, double y) const
{
    // 서버 Y는 위쪽이 양수이고 화면 Y는 아래쪽이 양수다. 원본 좌표 자체는 바꾸지 않는다.
    return {m_mapX + static_cast<float>(x - m_minX) * m_scale,
            m_mapY + m_mapHeight - static_cast<float>(y - m_minY) * m_scale};
}

const wire::UnitState* CReplayWindow::Selected() const
{
    for (const auto& unit : m_state.units())
    {
        if (unit.player_id() == m_selected)
        {
            return &unit;
        }
    }
    return nullptr;
}

void CReplayWindow::Seek(double milliseconds)
{
    m_playhead = clamp(milliseconds,
                       static_cast<double>(m_archive.frames.front().elapsed_ms()),
                       static_cast<double>(m_archive.frames.back().elapsed_ms()));
    m_cursor = m_archive.FindTime(static_cast<unsigned long long>(m_playhead));
    m_state = m_archive.StateAt(m_cursor);
    m_attacks.clear();
    m_events.clear();
    m_updated = chrono::steady_clock::now();
    if (m_cursor + 1 == m_archive.frames.size())
    {
        m_playing = false;
    }
    Refresh();
}

void CReplayWindow::Update()
{
    const auto now = chrono::steady_clock::now();
    if (m_playing && !m_dragging)
    {
        m_playhead = min(static_cast<double>(m_archive.frames.back().elapsed_ms()),
                         m_playhead + chrono::duration<double, milli>(now - m_updated).count() * m_speed);
        // 배속이 빨라도 중간 틱을 모두 적용한다. 상태 델타와 공격 이벤트가 빠지지 않는다.
        while (m_cursor + 1 < m_archive.frames.size() &&
               m_archive.frames[m_cursor + 1].elapsed_ms() <= m_playhead)
        {
            const auto before = m_state;
            Replay::Apply(m_archive.frames[++m_cursor], m_state);
            AddEvents(m_archive.frames[m_cursor], before);
        }
        if (m_cursor + 1 == m_archive.frames.size())
        {
            m_playing = false;
        }
    }
    m_updated = now;
    // 효과 수명도 리플레이 시각을 사용하므로 일시정지 때 효과가 사라지지 않는다.
    for (auto it = m_attacks.begin(); it != m_attacks.end();)
    {
        if (it->until < m_playhead)
        {
            it = m_attacks.erase(it);
        }
        else
        {
            ++it;
        }
    }
    Refresh();
}

void CReplayWindow::SetSpeed()
{
    wchar_t text[64]{};
    GetWindowTextW(m_controls[2], text, 64);
    try
    {
        string value;
        for (const wchar_t ch : wstring(text))
        {
            if (ch > 127)
            {
                throw runtime_error("speed must be numeric");
            }
            value += static_cast<char>(ch);
        }
        const double speed = ParseNumber(value);
        if (speed < 0.25 || speed > 16)
        {
            throw runtime_error("speed range");
        }
        Update(); // 변경 직전까지는 기존 배속으로 진행한다.
        m_speed = speed;
        Refresh();
    }
    catch (const exception&)
    {
        MessageBoxW(m_window,
                    L"배속은 0.25부터 16 사이의 숫자를 입력하세요.",
                    L"리플레이 배속",
                    MB_OK | MB_ICONINFORMATION);
        SetWindowTextW(m_controls[2], Wide(Number(m_speed, 2)).c_str());
        m_updated = chrono::steady_clock::now();
    }
}

void CReplayWindow::Refresh()
{
    const string status = string(m_playing ? "PLAYING" : "PAUSED") + " | " + Number(m_speed, 2) + "x | " +
                          Number(m_playhead / 1000, 2) + " / " +
                          Number(static_cast<double>(m_archive.frames.back().elapsed_ms()) / 1000, 2) + " s";
    SetWindowTextW(m_controls[6], Wide(status).c_str());
    if (!m_dragging)
    {
        SendMessageW(m_controls[4],
                     TBM_SETPOS,
                     TRUE,
                     static_cast<LPARAM>(m_playhead * 10000 / m_archive.frames.back().elapsed_ms()));
    }
    InvalidateRect(m_window, nullptr, FALSE);
}

void CReplayWindow::AddEvents(const wire::ReplayFrame& frame, const wire::WorldState& before)
{
    for (const auto& action : frame.actions())
    {
        if (!action.accepted() || action.kind() == "Move")
        {
            continue;
        }
        m_events.push_back(Number(static_cast<double>(frame.elapsed_ms()) / 1000) + "  " + action.name() +
                           "  " + action.kind());
        if (m_events.size() > 6)
        {
            m_events.pop_front();
        }
        if (!action.has_attack() && !action.has_skill())
        {
            continue;
        }
        const wire::UnitState* p_source = nullptr;
        for (const auto& unit : before.units())
        {
            if (unit.player_id() == action.player_id())
            {
                p_source = &unit;
                break;
            }
        }
        if (!p_source)
        {
            continue;
        }
        const string target = action.has_attack() ? action.attack().target_id() : action.skill().target_id();
        stReplayAttackLine line{p_source->x(),
                                p_source->y(),
                                p_source->x(),
                                p_source->y(),
                                static_cast<double>(frame.elapsed_ms()) + 220,
                                p_source->race()};
        for (const auto& unit : before.units())
        {
            if (unit.player_id() == target)
            {
                line.targetX = unit.x();
                line.targetY = unit.y();
            }
        }
        for (const auto& building : before.buildings())
        {
            if (building.id() == target)
            {
                line.targetX = building.x();
                line.targetY = building.y();
            }
        }
        if (action.has_skill() && target.empty())
        {
            line.targetX = action.skill().x();
            line.targetY = action.skill().y();
        }
        if (m_attacks.size() < 1024)
        {
            m_attacks.push_back(line);
        }
    }
}

void CReplayWindow::Portrait(
    Drawing::Graphics& graphics, const wire::UnitState& unit, float x, float y, float size)
{
    const auto found = m_images.find(unit.unit_name());
    if (found == m_images.end())
    {
        Drawing::SolidBrush brush(RaceColor(unit.race()));
        graphics.FillEllipse(&brush, x, y, size, size);
        return;
    }
    auto& image = *found->second;
    const float scale = min(size / image.GetWidth(), size / image.GetHeight());
    const float width = image.GetWidth() * scale, height = image.GetHeight() * scale;
    graphics.DrawImage(&image,
                       Drawing::RectF(x + (size - width) / 2, y + (size - height) / 2, width, height));
}

void CReplayWindow::DrawMap(Drawing::Graphics& graphics)
{
    Drawing::SolidBrush ground(Drawing::Color(24, 39, 38));
    graphics.FillRectangle(&ground, m_mapX, m_mapY, m_mapWidth, m_mapHeight);
    Drawing::Pen grid(Drawing::Color(41, 60, 58)), border(Drawing::Color(77, 104, 101));
    for (int i = 0; i <= 6; ++i)
    {
        const float x = m_mapX + m_mapWidth * i / 6, y = m_mapY + m_mapHeight * i / 6;
        graphics.DrawLine(&grid, x, m_mapY, x, m_mapY + m_mapHeight);
        graphics.DrawLine(&grid, m_mapX, y, m_mapX + m_mapWidth, y);
        Label(graphics,
              Number(m_minX + (m_maxX - m_minX) * i / 6, 0),
              x - 10,
              m_mapY + m_mapHeight + 5,
              11,
              Muted,
              42);
    }
    graphics.DrawRectangle(&border, m_mapX, m_mapY, m_mapWidth, m_mapHeight);
    Label(graphics, "Y " + Number(m_maxY, 0), m_mapX + 5, m_mapY + 5, 11, Muted);
    Label(graphics, "Y " + Number(m_minY, 0), m_mapX + 5, m_mapY + m_mapHeight - 22, 11, Muted);
    // 스킬 영역과 공격선은 연출용 표시다. 전투 결과/판정은 기록된 서버 상태만 따른다.
    const auto clip = graphics.Save();
    graphics.SetClip(Drawing::RectF(m_mapX, m_mapY, m_mapWidth, m_mapHeight));
    for (const auto& area : m_state.areas())
    {
        const auto p = Position(area.x(), area.y());
        const float radius = static_cast<float>(area.radius()) * m_scale;
        Drawing::SolidBrush fill(Drawing::Color(70, 94, 216, 183));
        Drawing::Pen edge(Drawing::Color(94, 216, 183));
        graphics.FillEllipse(&fill, p.X - radius, p.Y - radius, radius * 2, radius * 2);
        graphics.DrawEllipse(&edge, p.X - radius, p.Y - radius, radius * 2, radius * 2);
    }
    for (const auto& line : m_attacks)
    {
        Drawing::Pen pen(RaceColor(line.race), 2);
        graphics.DrawLine(&pen, Position(line.x, line.y), Position(line.targetX, line.targetY));
    }
    for (const auto& building : m_state.buildings())
    {
        const auto p = Position(building.x(), building.y());
        const auto color = building.hp() > 0 ? RaceColor(building.race()) : Muted;
        Drawing::SolidBrush fill(Drawing::Color(35, 44, 54));
        Drawing::Pen pen(color, 2);
        graphics.FillRectangle(&fill, p.X - 25, p.Y - 20, 50.0f, 40.0f);
        graphics.DrawRectangle(&pen, p.X - 25, p.Y - 20, 50.0f, 40.0f);
        Label(graphics, building.hp() > 0 ? "본진" : "파괴", p.X - 15, p.Y - 11, 13, color, 44);
        Label(graphics, building.name(), p.X - 59, p.Y + 23, 11, color, 125);
        for (const auto& original : m_initial.buildings())
        {
            if (original.id() != building.id())
            {
                continue;
            }
            Bar(graphics,
                p.X - 25,
                p.Y - 28,
                50,
                building.hp(),
                original.hp(),
                Drawing::Color(101, 213, 144));
            if (original.shield() > 0)
            {
                Bar(graphics,
                    p.X - 25,
                    p.Y - 35,
                    50,
                    building.shield(),
                    original.shield(),
                    RaceColor(wire::RACE_TERRAN));
            }
        }
    }
    // 대규모 전투에서는 같은 화면 위치의 같은 종족을 인원수로 묶어 그린다.
    // 실제 좌표와 선택 정보는 원본 상태를 사용하고, 화면을 위해 좌표를 바꾸지 않는다.
    struct stUnitMarker
    {
        const wire::UnitState* p_unit;
        int count;
    };
    vector<stUnitMarker> markers;
    unordered_map<string, size_t> cells;
    for (const auto& unit : m_state.units())
    {
        if (m_state.units_size() <= 100)
        {
            markers.push_back({&unit, 1});
            continue;
        }
        const auto p = Position(unit.x(), unit.y());
        const string key = to_string(unit.race()) + ":" + to_string(static_cast<int>(p.X / 24)) + ":" +
                           to_string(static_cast<int>(p.Y / 24));
        const auto found = cells.find(key);
        if (found == cells.end())
        {
            cells.emplace(key, markers.size());
            markers.push_back({&unit, 1});
        }
        else
        {
            auto& marker = markers[found->second];
            ++marker.count;
            if (unit.player_id() == m_selected)
            {
                marker.p_unit = &unit;
            }
        }
    }
    for (const auto& marker : markers)
    {
        const auto& unit = *marker.p_unit;
        const auto p = Position(unit.x(), unit.y());
        const bool selected = unit.player_id() == m_selected;
        Drawing::Pen ring(selected ? Drawing::Color(255, 255, 255) : RaceColor(unit.race()),
                          selected ? 3.0f : 2.0f);
        graphics.DrawEllipse(&ring, p.X - 16, p.Y - 16, 32.0f, 32.0f);
        Portrait(graphics, unit, p.X - 12, p.Y - 12, 24);
        Bar(graphics, p.X - 16, p.Y - 24, 32, unit.hp(), unit.max_hp(), Drawing::Color(101, 213, 144));
        if (unit.cloaked() || unit.burrowed())
        {
            Label(graphics, unit.burrowed() ? "B" : "C", p.X + 14, p.Y - 10, 11);
        }
        if (marker.count > 1)
        {
            Drawing::SolidBrush badge(Background);
            graphics.FillRectangle(&badge, p.X + 10, p.Y + 8, 48.0f, 22.0f);
            Label(graphics, "×" + to_string(marker.count), p.X + 12, p.Y + 8, 12, RaceColor(unit.race()), 48);
        }
    }
    graphics.Restore(clip);
    if (m_state.units_size() == 0 && m_state.winner() == wire::RACE_UNSPECIFIED)
    {
        Label(graphics,
              "유닛 입장 대기 · Play를 누르면 기록 순서대로 진행됩니다",
              m_mapX + 18,
              m_mapY + m_mapHeight / 2,
              15,
              Text,
              m_mapWidth - 30);
    }
    if (m_state.winner() != wire::RACE_UNSPECIFIED)
    {
        Label(graphics,
              RaceName(m_state.winner()) + " 승리 · 경기 종료",
              m_mapX + 24,
              m_mapY + m_mapHeight / 2,
              25,
              RaceColor(m_state.winner()),
              m_mapWidth - 30);
    }
}

void CReplayWindow::DrawPanel(Drawing::Graphics& graphics)
{
    const float x = m_width - 326;
    Drawing::SolidBrush panel(Panel);
    graphics.FillRectangle(&panel, x - 14, 104.0f, 316.0f, m_height - 202);
    Label(graphics, "전장 현황  /  " + to_string(m_state.units_size()) + "명", x, 116, 18);
    float y = 155;
    for (const auto& building : m_state.buildings())
    {
        int count = 0;
        string servers;
        for (const auto& unit : m_state.units())
        {
            if (unit.race() == building.race())
            {
                ++count;
                if (servers.empty())
                {
                    servers = unit.game_server();
                }
            }
        }
        Label(graphics,
              RaceName(building.race()) + "  " + to_string(count) + "명" +
                  (servers.empty() ? "" : "  [" + servers + "]"),
              x,
              y,
              14,
              RaceColor(building.race()));
        Label(graphics,
              building.name() + "  HP " + to_string(building.hp()) + " / S " + to_string(building.shield()),
              x,
              y + 24,
              12,
              Muted,
              290);
        y += 55;
    }
    Label(graphics, "선택한 유닛", x, y + 8, 17);
    const auto* p_unit = Selected();
    if (p_unit)
    {
        Portrait(graphics, *p_unit, x, y + 42, 72);
        Label(graphics, p_unit->unit_name(), x + 86, y + 43, 18, RaceColor(p_unit->race()), 200);
        Label(graphics, p_unit->name(), x + 86, y + 72, 12, Text, 200);
        Label(graphics,
              "서버 " + p_unit->game_server() + " · " + RaceName(p_unit->race()),
              x + 86,
              y + 96,
              12,
              Muted,
              200);
        Label(graphics,
              "HP " + to_string(p_unit->hp()) + "/" + to_string(p_unit->max_hp()) + "   보호막 " +
                  to_string(p_unit->shield()),
              x,
              y + 130);
        Label(graphics,
              "MP " + to_string(p_unit->mp()) + "/" + to_string(p_unit->max_mp()) + "   X " +
                  Number(p_unit->x()) + "  Y " + Number(p_unit->y()),
              x,
              y + 157,
              13);
        string effects;
        if (p_unit->cloaked())
        {
            effects += "클로킹 ";
        }
        if (p_unit->burrowed())
        {
            effects += "버로우 ";
        }
        if (p_unit->locked())
        {
            effects += "락다운 ";
        }
        if (p_unit->blinded())
        {
            effects += "블라인드 ";
        }
        if (p_unit->stimulated())
        {
            effects += "스팀팩 ";
        }
        if (p_unit->healing())
        {
            effects += "회복 ";
        }
        Label(graphics, effects.empty() ? "상태 이상 없음" : effects, x, y + 184, 12, Muted, 290);
    }
    else
    {
        Label(graphics, "맵 위 유닛을 클릭해 정보를 확인하세요.", x, y + 48, 13, Muted, 290);
    }
    y += 230;
    if (y + 44 < m_height - 100)
    {
        Label(graphics, "최근 전투 기록", x, y, 16);
        for (const auto& event : m_events)
        {
            y += 24;
            if (y + 24 >= m_height - 100)
            {
                break;
            }
            Label(graphics, event, x, y, 11, Muted, 290);
        }
    }
}

void CReplayWindow::Draw(Drawing::Graphics& graphics)
{
    graphics.SetSmoothingMode(Drawing::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Drawing::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Drawing::TextRenderingHintClearTypeGridFit);
    graphics.Clear(Background);
    Label(graphics, "STARCRAFT  /  전투 리플레이", 24, 16, 25, Text, 600);
    Label(graphics,
          "유닛 클릭: 정보  ·  Space: 재생/정지  ·  ← →: 5초 이동  ·  배속은 변경할 때만 입력",
          26,
          57,
          13,
          Muted,
          950);
    Label(graphics,
          "관전자 전체 시야 · 100명 초과 시 겹친 유닛은 ×인원 표시 · 유닛 이미지 © Blizzard Entertainment",
          26,
          78,
          11,
          Muted,
          950);
    DrawMap(graphics);
    DrawPanel(graphics);
}

void CReplayWindow::SaveImage(const fs::path& path, double seconds)
{
    // 포트폴리오 자료와 화면 검증에 쓰는 PNG다. 창과 동일한 그리기 함수/기록 상태를 사용한다.
    if (seconds < 0 || seconds * 1000 > m_archive.frames.back().elapsed_ms())
    {
        throw runtime_error("Image time outside recording");
    }
    m_cursor = m_archive.FindTime(static_cast<unsigned long long>(seconds * 1000));
    m_state = m_archive.StateAt(m_cursor);
    m_playhead = static_cast<double>(m_archive.frames[m_cursor].elapsed_ms());
    if (m_state.units_size())
    {
        m_selected = m_state.units(0).player_id();
    }
    LayoutMap();
    Drawing::Bitmap canvas(static_cast<int>(m_width), static_cast<int>(m_height));
    {
        Drawing::Graphics graphics(&canvas);
        Draw(graphics);
        Label(graphics,
              "REPLAY  " + Number(m_playhead / 1000, 2) + " s  /  " + to_string(m_state.units_size()) +
                  " units",
              26,
              m_height - 55,
              18,
              Text,
              700);
    }
    const CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    if (canvas.Save(path.c_str(), &png) != Drawing::Ok)
    {
        throw runtime_error("Cannot save replay image");
    }
}

void CReplayWindow::Paint(HDC dc)
{
    // 메모리 비트맵을 한 번에 복사해 갱신 시 배경 깜빡임을 막는다.
    Drawing::Bitmap canvas(static_cast<int>(m_width), static_cast<int>(m_height));
    {
        Drawing::Graphics graphics(&canvas);
        Draw(graphics);
    }
    // GDI+의 메모리 DC 렌더 결과를 GDI로 복사한다. WM_PRINTCLIENT에서도 같은 화면을 얻는다.
    HBITMAP bitmap = nullptr;
    if (canvas.GetHBITMAP(Background, &bitmap) != Drawing::Ok)
    {
        return;
    }
    HDC buffer = CreateCompatibleDC(dc);
    if (buffer)
    {
        const auto previous = SelectObject(buffer, bitmap);
        BitBlt(dc, 0, 0, static_cast<int>(m_width), static_cast<int>(m_height), buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, previous);
        DeleteDC(buffer);
    }
    DeleteObject(bitmap);
}

LRESULT CReplayWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CREATE)
    {
        CreateControls();
        return 0;
    }
    if (message == WM_SIZE)
    {
        if (wparam != SIZE_MINIMIZED && m_controls[0])
        {
            Layout();
        }
        return 0;
    }
    if (message == WM_GETMINMAXINFO)
    {
        auto* p_size = reinterpret_cast<MINMAXINFO*>(lparam);
        p_size->ptMinTrackSize = {1000, 780};
        return 0;
    }
    if (message == WM_ERASEBKGND)
    {
        return 1;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(m_window, &paint);
        Paint(dc);
        EndPaint(m_window, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT)
    {
        Paint(reinterpret_cast<HDC>(wparam));
        return 0;
    }
    if (message == WM_TIMER)
    {
        Update();
        return 0;
    }
    if (message == WM_COMMAND)
    {
        const int id = LOWORD(wparam);
        if (id == PlayButton)
        {
            Update();
            if (m_cursor + 1 == m_archive.frames.size())
            {
                Seek(0);
            }
            m_playing = true;
            m_updated = chrono::steady_clock::now();
        }
        else if (id == PauseButton)
        {
            Update();
            m_playing = false;
        }
        else if (id == SpeedButton)
        {
            SetSpeed();
        }
        else if (id == SpeedBox && HIWORD(wparam) == CBN_SELCHANGE)
        {
            const auto index = SendMessageW(m_controls[2], CB_GETCURSEL, 0, 0);
            wchar_t text[32]{};
            SendMessageW(m_controls[2], CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
            SetWindowTextW(m_controls[2], text);
            SetSpeed();
        }
        else if (id == BattleButton)
        {
            for (const auto& frame : m_archive.frames)
            {
                bool attack = false;
                for (const auto& action : frame.actions())
                {
                    if (action.accepted() && action.has_attack())
                    {
                        attack = true;
                        break;
                    }
                }
                if (attack)
                {
                    Seek(max(0.0, static_cast<double>(frame.elapsed_ms()) - 1000));
                    break;
                }
            }
        }
        Refresh();
        return 0;
    }
    if (message == WM_HSCROLL && reinterpret_cast<HWND>(lparam) == m_controls[4])
    {
        const int code = LOWORD(wparam);
        m_dragging = code == TB_THUMBTRACK;
        const auto position = SendMessageW(m_controls[4], TBM_GETPOS, 0, 0);
        Seek(static_cast<double>(m_archive.frames.back().elapsed_ms()) * position / 10000);
        return 0;
    }
    if (message == WM_LBUTTONDOWN)
    {
        const float x = static_cast<float>(GET_X_LPARAM(lparam)),
                    y = static_cast<float>(GET_Y_LPARAM(lparam));
        float nearest = 24 * 24;
        m_selected.clear();
        for (const auto& unit : m_state.units())
        {
            const auto p = Position(unit.x(), unit.y());
            const float distance = (x - p.X) * (x - p.X) + (y - p.Y) * (y - p.Y);
            if (distance < nearest)
            {
                nearest = distance;
                m_selected = unit.player_id();
            }
        }
        SetFocus(m_window);
        Refresh();
        return 0;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(m_window);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        KillTimer(m_window, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_window, message, wparam, lparam);
}

LRESULT CALLBACK CReplayWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* p_self = reinterpret_cast<CReplayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        p_self = static_cast<CReplayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        p_self->m_window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p_self));
    }
    if (!p_self)
    {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    try
    {
        return p_self->Handle(message, wparam, lparam);
    }
    catch (const exception& error)
    {
        // Windows 콜백 경계를 넘어 예외를 전파하지 않고 메시지 루프 종료 뒤 호출자에게 전달한다.
        p_self->m_failure = error.what();
        DestroyWindow(window);
        return 0;
    }
}

void CReplayWindow::Run()
{
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = L"PortfolioReplayWindow";
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        throw runtime_error("Cannot register replay window");
    }
    m_window = CreateWindowExW(0,
                               type.lpszClassName,
                               L"StarCraft - 2D Replay",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               CW_USEDEFAULT,
                               CW_USEDEFAULT,
                               1220,
                               880,
                               nullptr,
                               nullptr,
                               instance,
                               this);
    if (!m_window)
    {
        throw runtime_error(m_failure.empty() ? "Cannot create replay window" : m_failure);
    }
    ShowWindow(m_window, SW_SHOW);
    UpdateWindow(m_window);
    MSG message{};
    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        if (message.message == WM_KEYDOWN)
        {
            const HWND focus = GetFocus();
            const bool editing = focus == m_controls[2] || GetParent(focus) == m_controls[2];
            if (editing && message.wParam == VK_RETURN)
            {
                SetSpeed();
                continue;
            }
            if (!editing && message.wParam == VK_SPACE)
            {
                SendMessageW(m_window, WM_COMMAND, m_playing ? PauseButton : PlayButton, 0);
                continue;
            }
            if (!editing && (message.wParam == VK_LEFT || message.wParam == VK_RIGHT))
            {
                Seek(m_playhead + (message.wParam == VK_LEFT ? -5000 : 5000));
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result < 0 || !m_failure.empty())
    {
        throw runtime_error(m_failure.empty() ? "Replay message loop failed" : m_failure);
    }
}

void RunReplayWindow(const Replay::stArchive& archive, const fs::path& imageFile, double imageSeconds)
{
    // GDI+는 모든 이미지/그래픽 객체보다 먼저 시작하고, 해당 객체가 해제된 뒤 종료한다.
    Drawing::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Drawing::GdiplusStartup(&token, &input, nullptr) != Drawing::Ok)
    {
        throw runtime_error("Cannot initialize replay graphics");
    }
    try
    {
        INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES};
        if (!InitCommonControlsEx(&controls))
        {
            throw runtime_error("Cannot initialize replay controls");
        }
        {
            CReplayWindow window(archive);
            if (imageFile.empty())
            {
                window.Run();
            }
            else
            {
                window.SaveImage(imageFile, imageSeconds);
            }
        }
    }
    catch (...)
    {
        Drawing::GdiplusShutdown(token);
        throw;
    }
    Drawing::GdiplusShutdown(token);
}
