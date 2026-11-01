import PhotosUI
import SwiftUI

// MARK: - Ambient

/// The knob's face as big as it gets, and the look controls under it.
struct AmbientLiveView: View {
    var body: some View {
        ScrollView {
            VStack(spacing: 24) {
                HStack {
                    ConnectionBadge()
                    Spacer()
                }
                KnobFaceView(content: .field(value: 0.75), bezel: 10)
                    .frame(width: 290, height: 290)
                    .shadow(color: .black.opacity(0.45), radius: 30, y: 16)
                    .padding(.top, 8)
                LookSection()
            }
            .padding(.horizontal, 20)
            .padding(.top, 8)
            .padding(.bottom, 48)
        }
        .background(Theme.grouped.ignoresSafeArea())
    }
}

// MARK: - Photos

/// Five slots on the knob: the one showing, how it is drawn, and the gallery.
struct PhotoLiveView: View {
    @EnvironmentObject private var link: KnobLink
    @EnvironmentObject private var album: PhotoAlbum
    @State private var showPicker = false
    /// The slot being filled. Kept after the picker closes: the picked photo
    /// arrives a moment after that, and clearing it on close dropped every
    /// upload without a trace.
    @State private var targetSlot = 0
    @State private var pickerItem: PhotosPickerItem?

    private var state: PhotoState? { link.photos }

    var body: some View {
        ScrollView {
            VStack(spacing: 24) {
                HStack {
                    ConnectionBadge()
                    Spacer()
                }
                KnobFaceView(content: face, bezel: 10)
                    .frame(width: 290, height: 290)
                    .shadow(color: .black.opacity(0.45), radius: 30, y: 16)
                    .animation(.easeInOut(duration: 0.3), value: state)
                    .padding(.top, 8)

                Picker("View", selection: Binding(
                    get: { state?.mosaic ?? false },
                    set: { if $0 != state?.mosaic { link.send(KnobCommands.photoToggleView) } }
                )) {
                    Text("Photo").tag(false)
                    Text("Mosaic").tag(true)
                }
                .pickerStyle(.segmented)
                .frame(maxWidth: 260)
                .disabled(state == nil || state?.present == 0)

                gallery
                uploadStatus
            }
            .padding(.horizontal, 20)
            .padding(.top, 8)
            .padding(.bottom, 48)
        }
        .background(Theme.grouped.ignoresSafeArea())
        .photosPicker(isPresented: $showPicker, selection: $pickerItem, matching: .images)
        .onChange(of: pickerItem) { _, item in
            guard let item else { return }
            pickerItem = nil
            let slot = targetSlot
            album.preparing(slot)
            Task {
                if let data = try? await item.loadTransferable(type: Data.self), let image = UIImage(data: data) {
                    album.upload(image, to: slot, link: link)
                } else {
                    album.couldNotOpen(slot)
                }
            }
        }
        .onChange(of: link.framesLeft) { _, left in album.framesChanged(left: left, total: link.framesTotal) }
        .onChange(of: link.photos) { _, state in album.knobChanged(state) }
        .onChange(of: link.status) { _, status in album.connectionChanged(connected: status == .connected) }
    }

    private func pick(_ slot: Int) {
        targetSlot = slot
        showPicker = true
    }

    private var face: KnobFaceView.Content {
        guard let s = state, s.has(s.current), let image = album.images[s.current] else {
            return .field(value: 0.06)
        }
        return s.mosaic ? .mosaic(image) : .photo(image)
    }

    /// Every step of an upload, said plainly: sending (with how far), saving,
    /// saved, or what went wrong.
    @ViewBuilder
    private var uploadStatus: some View {
        if let up = album.upload {
            VStack(alignment: .leading, spacing: 10) {
                switch up {
                case .sending(_, let progress):
                    HStack {
                        Text("Sending to the knob")
                        Spacer()
                        Text("\(Int(progress * 100))%").monospacedDigit().foregroundStyle(.secondary)
                    }
                    ProgressView(value: progress)
                case .preparing:
                    HStack(spacing: 8) {
                        ProgressView().controlSize(.small)
                        Text("Getting the photo ready")
                    }
                case .saving:
                    HStack(spacing: 8) {
                        ProgressView().controlSize(.small)
                        Text("Saving on the knob")
                    }
                case .saved:
                    Label("Saved on the knob", systemImage: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                case .failed(_, let reason):
                    Label(reason, systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(.red)
                    HStack {
                        Button("Try again") { album.retry(link: link) }
                            .buttonStyle(.borderedProminent)
                        Button("Dismiss") { album.dismissError() }
                    }
                }
            }
            .font(.subheadline)
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.card, in: RoundedRectangle(cornerRadius: Theme.cardRadius, style: .continuous))
            .animation(.easeOut(duration: 0.2), value: up)
            .transition(.opacity)
        }
    }

    private var gallery: some View {
        CardSection(title: "On the knob") {
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 12), count: 5), spacing: 12) {
                ForEach(0..<KnobProtocol.photoSlots, id: \.self) { slot in
                    slotView(slot)
                }
            }
            .padding(16)
        }
        .disabled(state == nil)
    }

    @ViewBuilder
    private func slotView(_ slot: Int) -> some View {
        let onKnob = state?.has(slot) ?? false
        let current = onKnob && state?.current == slot
        let busy = album.upload?.inFlight == true && album.upload?.slot == slot
        ZStack {
            if let image = album.images[slot], onKnob {
                Image(uiImage: image).resizable().scaledToFill()
                    .frame(width: 54, height: 54)
                    .clipShape(Circle())
                    .overlay(Circle().strokeBorder(current ? Color.primary : .clear, lineWidth: 2).padding(-4))
                    .opacity(busy ? 0.35 : 1)
                if busy { ProgressView() }
            } else if busy {
                Circle().fill(Theme.fill2).frame(width: 54, height: 54)
                ProgressView()
            } else {
                Circle().fill(Theme.fill2)
                    .frame(width: 54, height: 54)
                    .overlay(Image(systemName: "plus").font(.system(size: 18, weight: .medium)).foregroundStyle(.secondary))
            }
        }
        .frame(height: 62)
        .contentShape(Circle())
        .onTapGesture {
            guard album.upload?.inFlight != true else { return }
            if onKnob { link.send(KnobCommands.photoShow(slot)) } else { pick(slot) }
        }
        .contextMenu {
            if onKnob {
                Button { pick(slot) } label: { Label("Replace", systemImage: "photo") }
                Button(role: .destructive) { album.delete(slot, link: link) } label: { Label("Delete", systemImage: "trash") }
            }
        }
        .sensoryFeedback(.selection, trigger: current)
    }
}


// MARK: - Modes played on the knob itself

/// Notifications and Games run entirely on the knob: its face, and how it
/// is played.
struct KnobOnlyLiveView: View {
    let title: String
    let detail: String
    let note: String?

    var body: some View {
        ScrollView {
            VStack(spacing: 24) {
                HStack {
                    ConnectionBadge()
                    Spacer()
                }
                KnobFaceView(content: .field(value: 0.45), bezel: 10)
                    .frame(width: 260, height: 260)
                    .shadow(color: .black.opacity(0.45), radius: 30, y: 16)
                    .padding(.top, 8)
                VStack(spacing: 8) {
                    Text(title).font(.title2.weight(.semibold))
                    Text(detail)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                    if let note {
                        Text(note)
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                            .padding(.top, 6)
                    }
                }
                .padding(.horizontal, 24)
            }
            .padding(.horizontal, 20)
            .padding(.top, 8)
            .padding(.bottom, 48)
        }
        .background(Theme.grouped.ignoresSafeArea())
    }
}
