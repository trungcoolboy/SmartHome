# Dashboard Architecture

## Muc tieu

Dashboard la lop dieu khien duy nhat cho hai domain:

- `Smart Home`: node `ESP-12S` qua `Wi-Fi + MQTT`
- `Futuristic Aqua`: `3 x STM32G431RB` qua `USB Serial + FT232RL`

Huong thiet ke tham chieu tu iOS/HomeKit theme, nhung duoc dieu chinh de phu hop he thong co thiet bi co khi, relay, LED va sensor.

Nguon tham chieu:

- SmartHomeScene: https://smarthomescene.com/blog/best-home-assistant-dashboard-themes-in-2023/
- iOS Themes repo: https://github.com/basnijholt/lovelace-ios-themes

## Nguyen tac giao dien

- card bo tron lon, mat do thong tin vua phai
- uu tien thao tac cham nhanh, khong giong trang admin
- status quan trong phai nhin thay ngay tren overview
- light/dark mode deu giu cung mot he token
- trang tong quan mang tinh "home control", trang chi tiet uu tien ky thuat va realtime

## Thong tin dieu huong

Primary navigation:

- `Overview`
- `Smart Home`
- `Aqua`
- `Scenes`
- `Alerts`
- `Settings`

Secondary navigation trong `Aqua`:

- `Core`
- `Motion`
- `Lighting`

## Bieu dien theo domain

### Overview

Muc tieu:

- hien trang thai toan he thong
- nhan biet ngay node nao offline, khu nao co canh bao
- co cac quick action cho tac vu hay dung

Khoi thong tin:

- he thong tong quan: so node online, offline, canh bao dang mo
- phong smart home: den, quat, nhiet do, do am
- aqua snapshot: nhiet do nuoc, muc nuoc, heater, pump, den
- task nhanh: feed, clean, lights preset, all-off, vacation mode

### Smart Home

Muc tieu:

- dieu khien theo phong
- hien card thiet bi dang thai ro rang
- cho phep xem state realtime va history ngan

Khoi thong tin:

- room cards
- favorite devices
- sensor trend mini charts
- scene va automation shortcut

### Aqua Core

Muc tieu:

- tap trung cho `STM32 #1`
- hien thong so nuoc va relay thiet bi song con

Khoi thong tin:

- water level
- water temperature
- relays: valve, pumps, O2, CO2, heater, filter
- rules and failsafe status
- event log

### Aqua Motion

Muc tieu:

- tap trung cho `STM32 #2`
- dieu khien co cau feed, skimmer, servo, motion AB

Khoi thong tin:

- AB gantry position
- homing state
- feed sequence status
- servo presets
- manual jog + macro actions

### Aqua Lighting

Muc tieu:

- tap trung cho `STM32 #3`
- quan ly `XYZ`, `11` kenh LED, `2` cum fan

Khoi thong tin:

- light head position
- LED channel intensity groups
- thermal status
- fan status
- preset scenes: sunrise, noon, sunset, maintenance

## Design tokens

### Color direction

- light mode: nen sang am, card mau sua, vien mo
- dark mode: den xanh than mem, khong den tuyet doi
- accent chinh: xanh aqua
- accent phu: cam am cho canh bao, xanh la cho online

### Shape

- bo tron lon `20px - 28px`
- card chinh co depth nhe
- nut quick action bo tron day

### Typography

- heading lon, dam, ngan gon
- metric dung so ro, co do chenh lech cap bac
- label phu tranh viet hoa qua muc

### Motion

- stagger reveal nhe
- card hover rat nhe
- status pulse chi dung cho canh bao song

## Model thong tin dashboard

Tat ca widget nen map ve mot model chung:

- `id`
- `domain`
- `name`
- `status`
- `lastSeen`
- `connectivity`
- `metrics`
- `actions`
- `alerts`

Vi du:

- `smart-light-living-room`
- `aqua-core-heater`
- `aqua-lighting-scene-sunset`

## Khung trien khai frontend

- `src/App.jsx`: shell tong
- `src/components/`: card, nav, section header, status pill
- `src/styles/`: token va global styles
- data mock tam thoi de science giao dien truoc khi noi backend

## Trinh tu lam tiep

1. dung shell dashboard
2. xay overview page
3. xay page `Aqua Core`
4. xay page `Smart Home`
5. chuan hoa device model
6. sau cung moi noi API va realtime
