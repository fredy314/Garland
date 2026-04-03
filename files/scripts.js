function sendCommand(cmd, value) {
    fetch(`/${cmd}?val=${value}`)
        .then(response => {
            if (!response.ok) {
                console.error(`Помилка запиту ${cmd}:`, response.statusText);
            }
        })
        .catch(error => {
            console.error(`Помилка мережі для ${cmd}:`, error);
        });
}

function setBrightness(value) {
    // Оновлюємо текст повзунка
    const display = document.getElementById('brightnessValue');
    if (display) display.innerText = value;
    
    // Оновлюємо сам повзунок, якщо функція викликана з кнопки "Вимкнути"
    const slider = document.getElementById('brightnessA');
    if (slider && slider.value !== String(value)) {
        slider.value = value;
    }

    sendCommand('brightness', value);
    if(value==0) {
        const mode = document.getElementById('modeA').value;
        updateActiveButton(mode, 0);
    }
}

function setMode(value) {
    sendCommand('mode', value);
    document.getElementById('modeA').value = value;
    var brightness = document.getElementById('brightnessA').value;
    if(brightness==0) {
        brightness = 255;
        document.getElementById('brightnessValue').innerText = 255;
        document.getElementById('brightnessA').value = 255;
    }
    updateActiveButton(value, brightness);
}

function setSpeed(value) {
    // Оновлюємо текст повзунка
    const display = document.getElementById('speedValueA');
    if (display) display.innerText = value;
    
    sendCommand('speed', value);
}

function setNightMode(checked) {
    const value = checked ? 1 : 0;
    sendCommand('nightmode_toggle', value);
}

function getStatus() {
    fetch('/status')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.json();
        })
        .then(data => {
            // Очікуємо формат: { "brightness": 255, "speed": 30, "mode": 1 }
            
            // Оновлюємо яскравість
            if (data.brightness !== undefined) {
                const bSlider = document.getElementById('brightnessA');
                const bVal = document.getElementById('brightnessValue');
                if (bSlider && bSlider.value !== String(data.brightness)) {
                    bSlider.value = data.brightness;
                }
                if (bVal && bVal.innerText !== String(data.brightness)) {
                    bVal.innerText = data.brightness;
                }
            }
            
            // Оновлюємо швидкість
            if (data.speed !== undefined) {
                const sSlider = document.getElementById('speedA');
                const sVal = document.getElementById('speedValueA');
                if (sSlider && sSlider.value !== String(data.speed)) {
                    sSlider.value = data.speed;
                }
                if (sVal && sVal.innerText !== String(data.speed)) {
                    sVal.innerText = data.speed;
                }
            }

            // Оновлюємо NightModeOnly
            if (data.nightModeOnly !== undefined) {
                const nmCb = document.getElementById('nightModeOnly');
                if (nmCb && nmCb.checked !== (data.nightModeOnly === 1)) {
                    nmCb.checked = (data.nightModeOnly === 1);
                }
            }

            // Оновлюємо активну кнопку
            updateActiveButton(data.mode, data.brightness);

            // Оновлюємо час сходу та заходу
            if (data.sunrise) document.getElementById('sunriseTime').innerText = data.sunrise;
            if (data.sunset) document.getElementById('sunsetTime').innerText = data.sunset;
        })
        .catch(error => {
            console.error('Помилка отримання статусу:', error);
        });
}

function updateActiveButton(mode, brightness) {
    // Знімаємо клас active з усіх кнопок
    document.querySelectorAll('#modesA .btn').forEach(btn => {
        btn.classList.remove('active');
    });

    if (brightness === 0) {
        // Якщо яскравість 0, підсвічуємо кнопку Вимкнути
        const btnOff = document.getElementById('btnActionOff');
        if (btnOff) btnOff.classList.add('active');
    } else {
        // Інакше підсвічуємо кнопку відповідного режиму
        const btnMode = document.getElementById(`btnMode${mode}`);
        if (btnMode) btnMode.classList.add('active');
    }
}

// Обробка головної кнопки вимкнення при завантаженні сторінки та запуск таймера
document.addEventListener('DOMContentLoaded', () => {
    // Оновлюємо статус одразу і потім кожні 5 секунд
    getStatus();
    setInterval(getStatus, 5000);
});
