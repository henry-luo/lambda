const nav = document.getElementById('nav');
const navBtn = document.getElementById('navBtn');
const closeBtn = document.getElementById('closeBtn');
const navHolder = document.getElementById('navHolder');
const calcForm = document.getElementById('calcForm');
const humanAgeInput = document.getElementById('humanAge');
const catAgeInput = calcForm.elements['catAge'];
const errorSpan = document.getElementById('errorSpan');

window.onload = function () {
	nav.classList.remove('before-animation');
};

navBtn.addEventListener('click', function () {
	nav.classList.add('expanded');
});

closeBtn.addEventListener('click', function () {
	nav.classList.remove('expanded');
});

navHolder.addEventListener('click', function (event) {
	if (event.target === event.currentTarget) {
		nav.classList.remove('expanded');
	}
});

calcForm.addEventListener('submit', function (event) {
	event.preventDefault();
	calcCatAge();
	catAgeInput.blur();
});

catAgeInput.addEventListener('input', function (event) {
	calcCatAge();
});

function calcCatAge() {
	errorSpan.innerText = '';
	let catAge = +catAgeInput.value;
	let humanYears;

	if (catAge >= 0.1 && catAge <= 1) {
		humanYears = 15 * +catAge.toFixed(1);
	} else if (catAge > 1 && catAge <= 2) {
		humanYears = 15 + 9 * (+catAge.toFixed(1) - 1);
	} else if (catAge > 2 && catAge <= 50) {
		humanYears = 4 * +catAge.toFixed(1) + 16;
	} else {
		humanAgeInput.innerText = '?';
		errorSpan.innerText = 'Please enter valid age of a cat';
		return;
	}

	humanAgeInput.innerText = +humanYears.toFixed(1);
	return;
}
