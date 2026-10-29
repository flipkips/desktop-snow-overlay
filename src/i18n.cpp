#include "i18n.h"
#include "settings.h"

const wchar_t* const kLangCodes[LANG_COUNT] = {L"EN", L"RU", L"UA", L"RO"};

// Rows follow enum Str; columns: EN, RU, UA, RO
static const wchar_t* const kText[][LANG_COUNT] = {
    // S_TRAY_TIP
    {L"LayerSnow: snow on your screen", L"LayerSnow: снег на экране", L"LayerSnow: сніг на екрані", L"LayerSnow: zăpadă pe ecran"},
    // S_WELCOME_TITLE
    {L"LayerSnow is running ❄", L"LayerSnow запущен ❄", L"LayerSnow запущено ❄", L"LayerSnow rulează ❄"},
    // S_WELCOME_TEXT
    {L"The snowflake lives in the tray. Left click opens settings, right click opens the menu.",
     L"Снежинка в трее. Левый клик открывает настройки, правый показывает меню.",
     L"Сніжинка в треї. Лівий клік відкриває налаштування, правий показує меню.",
     L"Fulgul stă în zona de notificare. Clic stânga deschide setările, clic dreapta deschide meniul."},
    // S_SETTINGS_TITLE
    {L"LayerSnow: settings", L"LayerSnow: настройки", L"LayerSnow: налаштування", L"LayerSnow: setări"},
    // S_SUBTITLE
    {L"Holiday snow, drifts and blizzards right on your desktop",
     L"Новогодний снег, сугробы и метель прямо на рабочем столе",
     L"Новорічний сніг, замети та хуртовина просто на робочому столі",
     L"Zăpadă de sărbători, troiene și viscol direct pe desktop"},
    // S_CARD_SNOWFALL
    {L"Snowfall", L"Снегопад", L"Снігопад", L"Ninsoare"},
    // S_MODE_TOP
    {L"Over windows", L"Поверх окон", L"Поверх вікон", L"Peste ferestre"},
    // S_MODE_BEHIND
    {L"Behind windows", L"За окнами", L"За вікнами", L"Sub ferestre"},
    // S_PRESET_LIGHT .. S_PRESET_BLIZZARD
    {L"Light", L"Лёгкий", L"Легкий", L"Ușoară"},
    {L"Normal", L"Обычный", L"Звичайний", L"Normală"},
    {L"Heavy", L"Сильный", L"Сильний", L"Puternică"},
    {L"Blizzard", L"Метель", L"Хуртовина", L"Viscol"},
    // S_INTENSITY .. S_FLAKE_SIZE
    {L"Intensity", L"Интенсивность", L"Інтенсивність", L"Intensitate"},
    {L"Fall speed", L"Скорость падения", L"Швидкість падіння", L"Viteza căderii"},
    {L"Wind angle", L"Угол ветра", L"Кут вітру", L"Unghiul vântului"},
    {L"Wind gusts", L"Порывы ветра", L"Пориви вітру", L"Rafale de vânt"},
    {L"Flake size", L"Размер снежинок", L"Розмір сніжинок", L"Mărimea fulgilor"},
    // S_CARD_PILES .. S_SMOOTHNESS
    {L"Drifts and cursor", L"Сугробы и курсор", L"Замети та курсор", L"Troiene și cursor"},
    {L"Pile-up speed", L"Скорость накопления", L"Швидкість накопичення", L"Viteza acumulării"},
    {L"On the taskbar", L"У панели задач", L"Біля панелі завдань", L"Pe bara de activități"},
    {L"On windows", L"На окнах", L"На вікнах", L"Pe ferestre"},
    {L"Hills and bumps", L"Горки и неровности", L"Гірки та нерівності", L"Dealuri și denivelări"},
    {L"Melting", L"Таяние", L"Танення", L"Topire"},
    {L"Cursor radius", L"Радиус курсора", L"Радіус курсора", L"Raza cursorului"},
    {L"Smoothness", L"Плавность", L"Плавність", L"Fluiditate"},
    // S_CARD_BEHAVIOR .. S_AUTOSTART
    {L"Behavior", L"Поведение", L"Поведінка", L"Comportament"},
    {L"Snow on", L"Снег включён", L"Сніг увімкнено", L"Zăpadă activată"},
    {L"Pile snow on windows", L"Копить снег на окнах", L"Накопичувати сніг на вікнах", L"Adună zăpadă pe ferestre"},
    {L"Pile snow on the taskbar", L"Копить снег на панели задач", L"Накопичувати сніг на панелі завдань",
     L"Adună zăpadă pe bara de activități"},
    {L"Knock snow off with the cursor", L"Сбивать снег курсором", L"Збивати сніг курсором", L"Dărâmă zăpada cu cursorul"},
    {L"Crystal flakes", L"Снежинки-кристаллы", L"Сніжинки-кристали", L"Fulgi de cristal"},
    {L"Hide in games and fullscreen apps", L"Скрывать в играх и полноэкранных приложениях",
     L"Ховати в іграх і повноекранних програмах", L"Ascunde în jocuri și aplicații pe tot ecranul"},
    {L"Start with Windows", L"Запускать вместе с Windows", L"Запускати разом з Windows", L"Pornește odată cu Windows"},
    // S_LANGUAGE, S_SHAKE, S_DEFAULTS, S_DONE
    {L"Language", L"Язык", L"Мова", L"Limbă"},
    {L"Shake off snow", L"Стряхнуть снег", L"Струсити сніг", L"Scutură zăpada"},
    {L"Defaults", L"По умолчанию", L"За замовчуванням", L"Implicit"},
    {L"Done", L"Готово", L"Готово", L"Gata"},
    // S_MODE .. S_EXIT (tray menu)
    {L"Mode", L"Режим", L"Режим", L"Mod"},
    {L"Snowfall", L"Сила снегопада", L"Сила снігопаду", L"Intensitatea ninsorii"},
    {L"Smoothness, FPS", L"Плавность, FPS", L"Плавність, FPS", L"Fluiditate, FPS"},
    {L"Shake off all snow", L"Стряхнуть весь снег", L"Струсити весь сніг", L"Scutură toată zăpada"},
    {L"All settings…", L"Все настройки…", L"Усі налаштування…", L"Toate setările…"},
    {L"Exit", L"Выход", L"Вихід", L"Ieșire"},
    // S_INSTALL_TITLE .. S_INSTALL_DONE
    {L"Install LayerSnow", L"Установка LayerSnow", L"Встановлення LayerSnow", L"Instalare LayerSnow"},
    {L"Update LayerSnow", L"Обновление LayerSnow", L"Оновлення LayerSnow", L"Actualizare LayerSnow"},
    {L"Holiday snow on your screen", L"Новогодний снег на вашем экране", L"Новорічний сніг на вашому екрані",
     L"Zăpadă de sărbători pe ecranul tău"},
    {L"What gets installed", L"Что будет установлено", L"Що буде встановлено", L"Ce se instalează"},
    {L"Snowflakes fall over or behind your windows, pile up on windows and the taskbar, and the cursor can "
     L"knock the snow off. The snowflake icon lives in the tray.",
     L"Снежинки падают поверх окон или за ними, копятся сугробами на окнах и панели задач, а курсором снег "
     L"можно сбить. Значок снежинки живёт в трее.",
     L"Сніжинки падають поверх вікон або за ними, накопичуються заметами на вікнах і панелі завдань, а курсором "
     L"сніг можна збити. Значок сніжинки живе в треї.",
     L"Fulgii cad peste ferestre sau în spatele lor, se adună în troiene pe ferestre și pe bara de activități, "
     L"iar cursorul poate dărâma zăpada. Pictograma fulgului stă în zona de notificare."},
    {L"Folder: ", L"Папка: ", L"Папка: ", L"Dosar: "},
    {L"Options", L"Параметры", L"Параметри", L"Opțiuni"},
    {L"Desktop shortcut", L"Ярлык на рабочем столе", L"Ярлик на робочому столі", L"Comandă rapidă pe desktop"},
    {L"Launch after install", L"Запустить после установки", L"Запустити після встановлення", L"Pornește după instalare"},
    {L"Cancel", L"Отмена", L"Скасувати", L"Anulează"},
    {L"Install", L"Установить", L"Встановити", L"Instalează"},
    {L"Update", L"Обновить", L"Оновити", L"Actualizează"},
    {L"Could not copy the file to\n", L"Не удалось скопировать файл в папку\n", L"Не вдалося скопіювати файл у папку\n",
     L"Fișierul nu a putut fi copiat în\n"},
    {L"Installation error", L"Ошибка установки", L"Помилка встановлення", L"Eroare la instalare"},
    {L"Done! You can start LayerSnow from the Start menu.", L"Готово! LayerSnow можно запустить из меню «Пуск».",
     L"Готово! LayerSnow можна запустити з меню «Пуск».", L"Gata! Poți porni LayerSnow din meniul Start."},
    // S_UNINSTALL_TITLE .. S_UNINSTALL_LINK
    {L"Uninstall LayerSnow", L"Удаление LayerSnow", L"Видалення LayerSnow", L"Dezinstalare LayerSnow"},
    {L"Remove the snow from this computer", L"Убрать снег с этого компьютера", L"Прибрати сніг із цього комп'ютера",
     L"Elimină zăpada de pe acest computer"},
    {L"What will be removed", L"Что будет удалено", L"Що буде видалено", L"Ce se elimină"},
    {L"The program, its Start menu and desktop shortcuts, and autostart.",
     L"Программа, её ярлыки в меню «Пуск» и на рабочем столе, автозапуск.",
     L"Програма, її ярлики в меню «Пуск» і на робочому столі, автозапуск.",
     L"Programul, comenzile rapide din meniul Start și de pe desktop, pornirea automată."},
    {L"Also delete my settings", L"Удалить и мои настройки", L"Видалити й мої налаштування", L"Șterge și setările mele"},
    {L"Uninstall", L"Удалить", L"Видалити", L"Dezinstalează"},
    {L"LayerSnow removed", L"LayerSnow удалён", L"LayerSnow видалено", L"LayerSnow a fost eliminat"},
    {L"The snow has melted. Happy holidays! ❄", L"Снег растаял. С наступающим! ❄", L"Сніг розтанув. Зі святами! ❄",
     L"Zăpada s-a topit. Sărbători fericite! ❄"},
    {L"Close", L"Закрыть", L"Закрити", L"Închide"},
    {L"Uninstall LayerSnow", L"Удалить LayerSnow", L"Видалити LayerSnow", L"Dezinstalează LayerSnow"},
    // S_CURSOR_WIND
    {L"Wind follows cursor", L"Ветер за курсором", L"Вітер за курсором", L"Vântul după cursor"},
    // S_SHOW_FALLING
    {L"Show falling snow", L"Показывать падающий снег", L"Показувати сніг, що падає", L"Arată zăpada care cade"},
    // S_AUTHOR, S_STAR_GITHUB
    {L"by " APP_AUTHOR, L"Автор: " APP_AUTHOR, L"Автор: " APP_AUTHOR, L"Autor: " APP_AUTHOR},
    {L"★ Star it on GitHub", L"★ Поставьте звезду на GitHub", L"★ Поставте зірку на GitHub", L"★ Dă-i o stea pe GitHub"},
};
static_assert(sizeof(kText) / sizeof(kText[0]) == S_COUNT, "translation rows must match enum Str");

int CurrentLang() {
    int l = Cfg().lang;
    if (l >= 0 && l < LANG_COUNT) return l;
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_RUSSIAN:   return LANG_RU;
    case LANG_UKRAINIAN: return LANG_UA;
    case LANG_ROMANIAN:  return LANG_RO;
    default:             return LANG_EN;
    }
}

const wchar_t* Tr(Str s) { return kText[s][CurrentLang()]; }
