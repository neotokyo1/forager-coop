#include "trade.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <thread>

#include "character.h"
#include "files.h"
#include "gui.h"
#include "interiors.h"
#include "log.h"
#include "session.h"
#include "skilltab.h"
#include "stations.h"
#include "trade_bag.h"
#include "trade_capture.h"
#include "trade_input.h"
#include "voidzone.h"
#include "world.h"

namespace coop::trade {
namespace {
using Peer = net::PeerId;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr uint8_t kRequest = 115, kView = 116, kPrepared = 117, kDecision = 118;
enum Action : uint8_t { OfferItem, Confirm, Cancel, Installed };
enum Status : uint8_t { Invited, Review, Prepare, Aborted, Busy, Completed, PartnerReady };

struct Request {
    uint32_t id;
    Action action;
    Peer other;
    uint16_t reserved = 0;
    Offer offer;
    Request() = default;
    Request(uint32_t tx, Action command, Peer target, Offer value)
        : id(tx), action(command), other(target), offer(value) {}
};
struct View {
    uint32_t id;
    Status status;
    Peer partner;
    uint16_t reserved = 0;
    Offer give, receive;
    View() = default;
    View(uint32_t tx, Status s, Peer target, Offer a, Offer b)
        : id(tx), status(s), partner(target), give(a), receive(b) {}
};
struct Decision {
    uint32_t id;
    uint32_t commit;
};
static_assert(sizeof(Request) == 24 && sizeof(View) == 40);

struct Pair {
    uint32_t id = 0;
    Peer peer[2]{};
    Offer offer[2]{};
    bool ready[2]{};
    Clock::time_point expires;
    int Side(Peer who) const { return who == peer[0] ? 0 : who == peer[1] ? 1 : -1; }
};
Pair pair;
uint32_t serial = 0;
std::map<Peer, uint32_t> saveFence;
bool abandonPending = false, waitRelease = false;
bool open = false, submitted = false, confirmed = false;
View view{};
int whichPeer = 0, whichStack = 0, quantity = 0, focus = 0;
Offer draft{};
Peer selected = 0;
std::string status, partnerName;
bool Host() { return session::CurrentRole() == session::Role::kHost; }
fs::path Guests() { return fs::path(character::WorldNoteFile("trade.pending")).parent_path(); }

bool Here(yyc::CInstance* p) {
    return yyc::GetBuiltinReal(p, yyc::va::YYVar_room) == yyc::kRoomGame && !interiors::IsInside() &&
           !voidzone::InVoid(p) && !skilltab::IsOpen() && !session::GameMenuOpen(p);
}
bool Present(Peer peer) {
    auto roster = session::TradePartners();
    return std::any_of(roster.begin(), roster.end(), [peer](const auto& row) { return row.first == peer; });
}
bool Same(const Offer& a, const Offer& b) {
    return a.stack.slot == b.stack.slot && a.stack.item == b.stack.item && a.stack.count == b.stack.count &&
           a.amount == b.amount;
}

void ClearView(std::string message) {
    view = {};
    submitted = confirmed = false;
    status = std::move(message);
}
void Show(const View& next, yyc::CInstance* p) {
    if (next.status == Busy) {
        abandonPending = false;
        if (!view.id) ClearView("Partner belegt");
        return;
    }
    if (next.status == PartnerReady) {
        if (view.id == next.id && view.status == Review && !confirmed) status = "Partner bereit - pruefen";
        return;
    }
    if (next.status == Aborted || next.status == Completed) {
        if (next.id == view.id) ClearView(next.status == Completed ? "Abgeschlossen" : "Abgebrochen");
        return;
    }
    if ((!view.id && next.status != Invited) || (view.id && next.id != view.id)) return;
    if (!view.id && next.status == Invited && !open && p) stations::Say(p, "Handelsanfrage - F3");
    view = next;
    selected = next.partner;
    if (next.status == Invited) status = submitted ? "Wartet auf Partner" : "Angebot auswaehlen";
    if (next.status == Review) status = confirmed ? "Bereit - wartet" : "Beide Angebote pruefen";
}
void Notify(int side, Status state, yyc::CInstance* p) {
    const View msg{pair.id, state, pair.peer[1 - side], pair.offer[side], pair.offer[1 - side]};
    if (pair.peer[side] == 0)
        Show(msg, p);
    else
        net::SendTo(pair.peer[side], kView, &msg, sizeof(msg));
}
void Finish(Status state, yyc::CInstance* p) {
    if (!pair.id) return;
    Notify(0, state, p);
    Notify(1, state, p);
    pair = {};
}
void RunBarrier(yyc::CInstance* p) {
    std::array<Image, 2> images;
    bool ok = true;
    for (int side = 0; side < 2; ++side) {
        const Peer peer = pair.peer[side];
        if (!peer) continue;
        const auto path = character::GuestDataFile(character::HostGuestName(peer), ".cin");
        images[side].guestFile = fs::path(path).filename().u8string();
        ok &= !images[side].guestFile.empty() && Present(peer);
        Notify(side, Prepare, p);
    }
    const int local = pair.Side(0);
    if (local >= 0) {
        if (!open || !confirmed || !Here(p)) ok = false;
        if (ok) images[local].bytes = Checkpoint(p, pair.offer[local], pair.offer[1 - local]);
        ok &= !images[local].bytes.empty();
    }
    const auto until = Clock::now() + std::chrono::seconds(7);
    while (ok && (images[0].bytes.empty() || images[1].bytes.empty())) {
        for (auto& m : net::DrainType(kPrepared)) {
            int side = pair.Side(m.from);
            if (side < 0 || !m.from || !images[side].bytes.empty() || m.payload.size() < sizeof(pair.id)) continue;
            uint32_t id;
            memcpy(&id, m.payload.data(), sizeof(id));
            if (id != pair.id) continue;
            const size_t size = m.payload.size() - sizeof(id);
            if (size > kMaxImage || !files::IsCompleteCinder(m.payload.data() + sizeof(id), size)) {
                ok = false;
                break;
            }
            images[side].bytes.assign(m.payload.begin() + sizeof(id), m.payload.end());
        }
        const auto links = net::Peers();
        for (Peer peer : pair.peer)
            if (peer && std::find(links.begin(), links.end(), peer) == links.end()) ok = false;
        if (Clock::now() >= until) ok = false;
        if (ok && (images[0].bytes.empty() || images[1].bytes.empty()))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto dir = Guests();
    bool commit = ok && CommitImages(dir, images);
    if (commit) {
        // From this point on the decision survives a host restart. A missing acknowledgement
        // can only require replaying these exact images, never granting items a second time.
        for (Peer peer : pair.peer)
            if (peer) saveFence[peer] = pair.id;
        const bool installed = InstallImages(dir);
        if (!installed) {
            Log("trade: checkpoint installation pending; returning to the menu for recovery");
            session::RejoinAfterTrade(p);
        }
        if (local >= 0) TrackHostSaves(dir);
        if (installed && local >= 0 && !Exchange(p, pair.offer[local], pair.offer[1 - local]))
            session::RejoinAfterTrade(p);
    }
    Decision decision{pair.id, static_cast<uint8_t>(commit)};
    for (Peer peer : pair.peer)
        if (peer) net::SendTo(peer, kDecision, &decision, sizeof(decision));
    Log("trade: transaction %u %s", pair.id, commit ? "committed" : "aborted");
    Finish(commit ? Completed : Aborted, p);
}
void OnRequest(Peer from, const Request& r, yyc::CInstance* p) {
    if (from && !Present(from)) {
        if (r.action == OfferItem && !r.id) {
            View busy{0, Busy, r.other, {}, {}};
            net::SendTo(from, kView, &busy, sizeof(busy));
        }
        return;
    }
    if (r.action == Installed) {
        auto it = saveFence.find(from);
        if (it != saveFence.end() && it->second == r.id && Recover()) saveFence.erase(it);
        return;
    }
    if (r.action == OfferItem && !r.id) {
        if (r.other == from || !ValidOffer(r.offer)) return;
        if (pair.id || saveFence.count(from) || saveFence.count(r.other) || (r.other && !Present(r.other)) ||
            !Recover()) {
            View msg{0, Busy, r.other, {}, {}};
            if (from)
                net::SendTo(from, kView, &msg, sizeof(msg));
            else
                Show(msg, p);
            return;
        }
        pair.id = ++serial;
        if (!pair.id) pair.id = ++serial;
        pair.peer[0] = from;
        pair.peer[1] = r.other;
        pair.offer[0] = r.offer;
        pair.expires = Clock::now() + std::chrono::seconds(90);
        Notify(0, Invited, p);
        Notify(1, Invited, p);
        return;
    }
    const int side = pair.Side(from);
    if (!pair.id || side < 0 || r.id != pair.id || r.other != pair.peer[1 - side]) return;
    if (r.action == Cancel) {
        Finish(Aborted, p);
        return;
    }
    if (r.action == OfferItem && side == 1 && !ValidOffer(pair.offer[1]) && ValidOffer(r.offer)) {
        pair.offer[1] = r.offer;
        Notify(0, Review, p);
        Notify(1, Review, p);
    }
    if (r.action == Confirm && ValidOffer(pair.offer[1]) && Same(r.offer, pair.offer[side])) {
        pair.ready[side] = true;
        if (pair.ready[0] && pair.ready[1])
            RunBarrier(p);
        else
            Notify(1 - side, PartnerReady, p);
    }
}
void Send(const Request& request, yyc::CInstance* p) {
    if (Host())
        OnRequest(0, request, p);
    else if (!net::SendTo(0, kRequest, &request, sizeof(request)))
        ClearView("Verbindung verloren");
}

void ClosePhone(yyc::CInstance* player) {
    if (submitted && !view.id) abandonPending = true;
    if (view.id) Send({view.id, Cancel, view.partner, {}}, player);
    open = false;
    waitRelease = true;
    ClearView({});
}

void GuestBarrier(const View& next, yyc::CInstance* p) {
    const bool agreed = open && confirmed && view.status == Review && view.id == next.id &&
                        view.partner == next.partner && Same(view.give, next.give) && Same(view.receive, next.receive);
    Bytes image;
    if (agreed && Here(p)) image = Checkpoint(p, view.give, view.receive);
    Bytes payload(sizeof(next.id));
    memcpy(payload.data(), &next.id, sizeof(next.id));
    payload.insert(payload.end(), image.begin(), image.end());
    net::SendTo(0, kPrepared, payload.data(), payload.size());
    if (image.empty()) return;
    const auto until = Clock::now() + std::chrono::seconds(15);
    while (net::IsConnected() && Clock::now() < until) {
        for (auto& m : net::DrainType(kDecision)) {
            Decision decision{};
            if (m.from || m.payload.size() != sizeof(decision)) continue;
            memcpy(&decision, m.payload.data(), sizeof(decision));
            if (decision.id != next.id) continue;
            if (!decision.commit) {
                ClearView("Abgebrochen");
                return;
            }
            if (!Exchange(p, next.give, next.receive) ||
                !files::WriteAtomic(fs::path(world::GuestSlotPath()) / "Data.cin", image.data(), image.size())) {
                session::RejoinAfterTrade(p);
                return;
            }
            CaptureGuestSaves(image);
            Send({next.id, Installed, next.partner, {}}, p);
            ClearView("Abgeschlossen");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // No unilateral rollback/save after a prepared vote. Rejoin reads the host's decision.
    session::RejoinAfterTrade(p);
}

struct Layout {
    double scale, x, y;
    gui::Rect Box(double l, double t, double r, double b) const {
        return {x + l * scale, y + t * scale, x + r * scale, y + b * scale};
    }
};
Layout Phone() {
    double w = gui::GuiWidth(), h = gui::GuiHeight();
    double s = (std::min)(w / 326.0, h / 324.0);
    return {s, (w - 296 * s) / 2, (h - 300 * s) / 2};
}
int Wrap(int i, int n) { return n ? (i % n + n) % n : 0; }
std::string Item(const Offer& o) {
    return ValidOffer(o) ? "#" + std::to_string(o.stack.item) + "  x " + std::to_string(o.amount) : "-";
}
}  // namespace

bool Open() { return open; }
bool Recover() {
    const auto dir = Guests();
    return dir.empty() || InstallImages(dir);
}
bool BeforeLoad() {
    const auto dir = Guests();
    if (dir.empty()) return true;
    if (!InstallImages(dir) || !RestoreHostImage(dir)) return false;
    TrackHostSaves({});
    return true;
}
bool AcceptCharacterSave(Peer peer) { return saveFence.find(peer) == saveFence.end() && Recover(); }
void PeerLeft(Peer peer) {
    if (Host() && pair.id && pair.Side(peer) >= 0) Finish(Aborted, nullptr);
    if (view.id && (view.partner == peer || (!Host() && !peer))) ClearView("Partner getrennt");
}
void Reset() {
    pair = {};
    ReleaseScratch();
    ResetCapture();
    ClearView({});
    open = abandonPending = waitRelease = false;
    input::Capture(false);
    saveFence.clear();
}
bool Handle(const net::Message& m, yyc::CInstance* p) {
    if (m.type < kRequest || m.type > kDecision) return false;
    if (m.type == kRequest && Host() && m.from && m.payload.size() == sizeof(Request)) {
        Request r;
        memcpy(&r, m.payload.data(), sizeof(r));
        OnRequest(m.from, r, p);
    }
    if (m.type == kView && !Host() && !m.from && m.payload.size() == sizeof(View)) {
        View v;
        memcpy(&v, m.payload.data(), sizeof(v));
        if (abandonPending && v.status == Invited) {
            abandonPending = false;
            Send({v.id, Cancel, v.partner, {}}, p);
        } else if (v.status == Prepare)
            GuestBarrier(v, p);
        else
            Show(v, p);
    }
    return true;
}

bool Input(yyc::CInstance* p) {
    input::Capture(open || waitRelease);
    if (session::CurrentRole() == session::Role::kNone) return false;
    input::ReadPhone read;
    if (Host() && pair.id && Clock::now() > pair.expires) Finish(Aborted, p);
    gui::Begin(p);
    if (waitRelease) {
        if (gui::Call("mouse_check_button", {1}) || gui::Call("keyboard_check", {VK_RETURN})) return true;
        waitRelease = false;
        input::Capture(open);
    }
    const bool toggle = gui::KeyPressed(VK_F3);
    if (!open && !toggle) return false;
    if (open && (toggle || gui::KeyPressed(VK_ESCAPE) || !net::IsConnected() || !Here(p))) {
        ClosePhone(p);
        return true;
    }
    if (!open) {
        if (!net::IsConnected() || !Here(p)) return false;
        if (!input::Install()) {
            stations::Say(p, "Trade: Eingabe nicht verfuegbar");
            return false;
        }
        input::Capture(true);
        open = true;
        focus = whichStack = whichPeer = quantity = 0;
        if (!view.id) status = "Partner auswaehlen";
    }
    auto roster = session::TradePartners();
    if (roster.empty() && !view.id && !submitted) status = "Kein Partner verfuegbar";
    auto stacks = Bag(p);
    auto l = Phone();
    int direction = int(gui::KeyPressed(VK_RIGHT)) - int(gui::KeyPressed(VK_LEFT));
    focus = Wrap(focus + int(gui::KeyPressed(VK_DOWN)) - int(gui::KeyPressed(VK_UP)), 4);
    bool activate = gui::KeyPressed(VK_RETURN);
    if (gui::MousePressed()) {
        const double mx = gui::MouseX(), my = gui::MouseY();
        for (int row = 0; row < 4; ++row) {
            if (!l.Box(12, 50 + row * 40, 284, 86 + row * 40).Contains(mx, my)) continue;
            focus = row;
            if (row == 3)
                activate = true;
            else
                direction = mx < l.x + 148 * l.scale ? -1 : 1;
        }
        if (l.Box(246, 4, 290, 40).Contains(mx, my)) {
            ClosePhone(p);
            return true;
        }
    }
    if (!view.id && !submitted && !roster.empty()) {
        whichPeer = Wrap(whichPeer + (focus == 0 ? direction : 0), static_cast<int>(roster.size()));
        selected = roster[whichPeer].first;
    }
    if (!submitted && !stacks.empty()) {
        if (focus == 1 && direction) quantity = 0;
        whichStack = Wrap(whichStack + (focus == 1 ? direction : 0), static_cast<int>(stacks.size()));
        const auto& stack = stacks[whichStack];
        if (quantity <= 0 || quantity > stack.count) quantity = stack.count;
        if (focus == 2 && direction) quantity = 1 + Wrap(quantity - 1 + direction, stack.count);
        draft = {stack, quantity};
    } else if (!submitted)
        draft = {};
    partnerName = "-";
    for (const auto& entry : roster)
        if (entry.first == selected) partnerName = entry.second + " #" + std::to_string(selected);
    if (activate && view.status == Review && view.id && !confirmed) {
        if (!CanExchange(p, view.give, view.receive)) {
            status = "Bestand / Platz pruefen";
            return true;
        }
        confirmed = true;
        status = "Bereit - wartet";
        Send({view.id, Confirm, view.partner, view.give}, p);
    } else if (activate && !submitted && !abandonPending && ValidOffer(draft) && Present(selected)) {
        submitted = true;
        status = "Wartet auf Partner";
        Send({view.id, OfferItem, selected, draft}, p);
    }
    return true;
}
void Draw(yyc::CInstance* p) {
    if (!open) return;
    gui::Begin(p);
    auto l = Phone();
    auto text = [&](int x, int y, const std::string& s, int color, double size) {
        gui::Text(l.x + x * l.scale, l.y + y * l.scale, s, size * l.scale, color);
    };
    constexpr auto shell = gui::Rgb(22, 32, 39), surface = gui::Rgb(41, 56, 63), mint = gui::Rgb(133, 228, 184);
    gui::Fill(l.Box(3, 4, 299, 304), 0, 0.48);
    gui::Fill(l.Box(0, 0, 296, 300), shell);
    gui::Frame(l.Box(0, 0, 296, 300), mint);
    text(14, 24, "Handel", mint, 1.3);
    text(260, 24, "x", gui::kWhite, 1.2);
    const Offer own = ValidOffer(view.give) ? view.give : draft;
    const std::string rows[] = {
        "Partner   " + partnerName,
        ValidOffer(own) ? "Fach " + std::to_string(own.stack.slot + 1) + "   Item #" + std::to_string(own.stack.item)
                        : "Kein Stapel",
        "Menge   " + std::to_string(own.amount) + " / " + std::to_string(own.stack.count),
        confirmed               ? "Bereit"
        : view.status == Review ? "Bestaetigen"
                                : "Anbieten"};
    for (int i = 0; i < 4; ++i) {
        int top = 50 + i * 40;
        gui::Fill(l.Box(12, top, 284, top + 36), surface);
        if (focus == i) gui::Frame(l.Box(12, top, 284, top + 36), mint);
        text(19, top + 18, rows[i], gui::kWhite, 0.85);
        if (i < 3 && !submitted && !(view.id && i == 0)) text(250, top + 18, "< >", mint, 0.8);
    }
    text(16, 227, "Erhalt   " + Item(view.receive), gui::kWhite, 0.85);
    text(16, 254, status, mint, 0.85);
    text(16, 280, "Pfeile / Enter     F3 schliessen", gui::kWhite, 0.65);
    gui::End();
}
}  // namespace coop::trade
