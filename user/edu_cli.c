// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../include/edu_ioctl.h"

#define DEVICE_PATH "/dev/edupci"

static void usage(FILE *stream)
{
	fprintf(stream,
		"Usage:\n"
		"  edu_cli info\n"
		"  edu_cli invert VALUE\n"
		"  edu_cli factorial N\n"
		"  edu_cli events\n"
		"  edu_cli stats\n"
		"  edu_cli ring-test\n"
		"  edu_cli dma-test\n"
		"  edu_cli selftest\n");
}

static int open_device(int flags)
{
	int fd = open(DEVICE_PATH, flags);

	if (fd < 0)
		fprintf(stderr, "open %s: %s\n", DEVICE_PATH, strerror(errno));
	return fd;
}

static int parse_u32(const char *text, uint32_t *value)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno || !text[0] || !end || *end || parsed > UINT32_MAX)
		return -1;
	*value = (uint32_t)parsed;
	return 0;
}

static int get_stats(int fd, struct edu_stats *stats)
{
	if (ioctl(fd, EDU_IOC_GET_STATS, stats) < 0) {
		fprintf(stderr, "GET_STATS: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int factorial_fd(int fd, uint32_t input, uint32_t *result)
{
	struct edu_factorial request = { .input = input };

	if (ioctl(fd, EDU_IOC_FACTORIAL, &request) < 0) {
		fprintf(stderr, "factorial(%u): %s\n", input, strerror(errno));
		return -1;
	}
	*result = request.result;
	return 0;
}

static uint32_t expected_factorial(uint32_t input)
{
	uint32_t result = 1;
	uint32_t n;

	for (n = 2; n <= input; n++)
		result *= n;
	return result;
}

static int command_info(void)
{
	struct edu_info info;
	int fd = open_device(O_RDONLY);

	if (fd < 0)
		return 1;
	if (ioctl(fd, EDU_IOC_GET_INFO, &info) < 0) {
		fprintf(stderr, "GET_INFO: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	printf("QEMU EDU PCI device %04x:%04x\n", info.vendor_id, info.device_id);
	printf("BAR0: %u bytes, identification: 0x%08x\n",
	       info.bar0_size, info.device_version);
	printf("IRQ: %s, event capacity: %u\n",
	       info.irq_mode == EDU_IRQ_MODE_MSI ? "MSI" :
	       info.irq_mode == EDU_IRQ_MODE_INTX ? "INTx" : "unknown",
	       info.event_capacity);
	close(fd);
	return 0;
}

static int command_invert(const char *text)
{
	struct edu_invert request;
	uint32_t expected;
	int fd;

	if (parse_u32(text, &request.input)) {
		fprintf(stderr, "Invalid 32-bit integer: %s\n", text);
		return 2;
	}
	expected = ~request.input;
	fd = open_device(O_RDWR);
	if (fd < 0)
		return 1;
	if (ioctl(fd, EDU_IOC_INVERT, &request) < 0) {
		fprintf(stderr, "INVERT: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	if (request.result != expected) {
		fprintf(stderr, "expected 0x%08x, got 0x%08x\n",
			expected, request.result);
		return 1;
	}
	printf("invert(0x%08x) = 0x%08x: PASS\n",
	       request.input, request.result);
	return 0;
}

static int command_factorial(const char *text)
{
	struct edu_stats before;
	struct edu_stats after;
	uint32_t input;
	uint32_t result;
	int fd;
	int status = 1;

	if (parse_u32(text, &input) || input > 12) {
		fprintf(stderr, "Factorial input must be in the range 0..12.\n");
		return 2;
	}
	fd = open_device(O_RDWR);
	if (fd < 0)
		return 1;
	if (get_stats(fd, &before))
		goto out;
	if (factorial_fd(fd, input, &result))
		goto out;
	if (get_stats(fd, &after))
		goto out;
	if (result != expected_factorial(input) ||
	    after.factorial_count <= before.factorial_count) {
		fprintf(stderr, "factorial result or IRQ completion counter mismatch\n");
		goto out;
	}
	printf("factorial(%u) = %u, IRQ completion: PASS\n", input, result);
	status = 0;
out:
	close(fd);
	return status;
}

static int command_stats(void)
{
	struct edu_stats stats;
	int fd = open_device(O_RDONLY);

	if (fd < 0)
		return 1;
	if (get_stats(fd, &stats)) {
		close(fd);
		return 1;
	}
	printf("IRQs=%" PRIu64 " factorial=%" PRIu64 " DMA=%" PRIu64
	       " overflow=%" PRIu64 " last_sequence=%" PRIu64 "\n",
	       (uint64_t)stats.irq_count, (uint64_t)stats.factorial_count,
	       (uint64_t)stats.dma_count, (uint64_t)stats.event_overflows,
	       (uint64_t)stats.last_sequence);
	close(fd);
	return 0;
}

static int read_events_exact(int fd, struct edu_event *events, size_t count)
{
	size_t done = 0;
	size_t bytes = count * sizeof(*events);

	while (done < bytes) {
		ssize_t got = read(fd, (char *)events + done, bytes - done);

		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0) {
			fprintf(stderr, "read events: %s\n", strerror(errno));
			return -1;
		}
		if (!got) {
			fprintf(stderr, "event stream closed before expected records\n");
			return -1;
		}
		done += (size_t)got;
	}
	return 0;
}

static int drain_events(int fd)
{
	struct edu_event events[EDU_EVENT_CAPACITY];
	int total = 0;

	for (;;) {
		ssize_t got = read(fd, events, sizeof(events));

		if (got > 0) {
			if (got % (ssize_t)sizeof(events[0])) {
				fprintf(stderr, "driver returned a partial event record\n");
				return -1;
			}
			total += (int)(got / (ssize_t)sizeof(events[0]));
			continue;
		}
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0 && errno == EAGAIN)
			return total;
		fprintf(stderr, "drain events: %s\n",
			got == 0 ? "unexpected EOF" : strerror(errno));
		return -1;
	}
}

static int event_readable(int fd, int timeout_ms)
{
	struct pollfd descriptor = {
		.fd = fd,
		.events = POLLIN,
	};
	int ret = poll(&descriptor, 1, timeout_ms);

	if (ret < 0) {
		fprintf(stderr, "poll: %s\n", strerror(errno));
		return -1;
	}
	if (!ret)
		return 0;
	return !!(descriptor.revents & POLLIN);
}

static int check_fifo(const struct edu_event *events, size_t count)
{
	size_t i;

	for (i = 1; i < count; i++) {
		if (events[i].sequence != events[i - 1].sequence + 1)
			return -1;
	}
	return 0;
}

static int issue_factorials(int fd, const uint32_t *inputs, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		uint32_t result;

		if (factorial_fd(fd, inputs[i], &result))
			return -1;
		if (result != expected_factorial(inputs[i])) {
			fprintf(stderr, "factorial(%u) returned %u\n",
				inputs[i], result);
			return -1;
		}
	}
	return 0;
}

struct blocking_read_result {
	int status;
	struct edu_event event;
};

static int blocking_read_test(int trigger_fd)
{
	struct blocking_read_result result;
	struct pollfd notification;
	char ready;
	int pipefd[2];
	int blocking_fd;
	pid_t child;
	int child_status;
	uint32_t factorial_result;
	int poll_result;

	blocking_fd = open_device(O_RDONLY);
	if (blocking_fd < 0)
		return -1;
	if (pipe(pipefd) < 0) {
		fprintf(stderr, "pipe: %s\n", strerror(errno));
		close(blocking_fd);
		return -1;
	}
	fflush(NULL);
	child = fork();
	if (child < 0) {
		fprintf(stderr, "fork: %s\n", strerror(errno));
		close(pipefd[0]);
		close(pipefd[1]);
		close(blocking_fd);
		return -1;
	}
	if (child == 0) {
		ssize_t got;

		close(pipefd[0]);
		close(trigger_fd);
		ready = 'R';
		if (write(pipefd[1], &ready, sizeof(ready)) != sizeof(ready))
			_exit(2);
		memset(&result, 0, sizeof(result));
		got = read(blocking_fd, &result.event, sizeof(result.event));
		if (got == sizeof(result.event))
			result.status = 0;
		else
			result.status = got < 0 ? -errno : -EIO;
		if (write(pipefd[1], &result, sizeof(result)) != sizeof(result))
			_exit(3);
		close(blocking_fd);
		close(pipefd[1]);
		_exit(result.status ? 1 : 0);
	}

	close(pipefd[1]);
	close(blocking_fd);
	notification.fd = pipefd[0];
	notification.events = POLLIN;
	notification.revents = 0;
	poll_result = poll(&notification, 1, 2000);
	if (poll_result != 1 || read(pipefd[0], &ready, sizeof(ready)) != sizeof(ready) ||
	    ready != 'R') {
		fprintf(stderr, "blocking reader did not start\n");
		goto fail_child;
	}
	if (event_readable(pipefd[0], 100) != 0) {
		fprintf(stderr, "blocking read returned before an event arrived\n");
		goto fail_child;
	}
	if (factorial_fd(trigger_fd, 5, &factorial_result) || factorial_result != 120)
		goto fail_child;
	if (event_readable(pipefd[0], 2000) != 1 ||
	    read(pipefd[0], &result, sizeof(result)) != sizeof(result)) {
		fprintf(stderr, "blocking reader did not wake for the event\n");
		goto fail_child;
	}
	if (waitpid(child, &child_status, 0) != child ||
	    !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0 ||
	    result.status || result.event.type != EDU_EVENT_FACTORIAL ||
	    result.event.value != 120) {
		fprintf(stderr, "blocking read returned an invalid event\n");
		close(pipefd[0]);
		return -1;
	}
	close(pipefd[0]);
	puts("blocking read wakeup: PASS");
	return 0;

fail_child:
	kill(child, SIGKILL);
	waitpid(child, NULL, 0);
	close(pipefd[0]);
	return -1;
}

static int command_ring_test(void)
{
	static const uint32_t basic_inputs[] = { 0, 1, 5, 10 };
	struct edu_event events[EDU_EVENT_CAPACITY];
	struct edu_stats before;
	struct edu_stats after;
	uint32_t wrap_inputs[50];
	uint32_t full_inputs[EDU_EVENT_CAPACITY + 5];
	ssize_t got;
	int fd;
	size_t i;
	int drained;

	fd = open_device(O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return 1;
	drained = drain_events(fd);
	if (drained < 0)
		goto fail;
	if (blocking_read_test(fd))
		goto fail;
	if (event_readable(fd, 0) != 0) {
		fprintf(stderr, "empty ring unexpectedly reported readable\n");
		goto fail;
	}
	{
		struct edu_event event;
		got = read(fd, &event, sizeof(event));
		if (got >= 0 || errno != EAGAIN) {
			fprintf(stderr, "empty nonblocking read did not return EAGAIN\n");
			goto fail;
		}
	}
	printf("empty/nonblocking read: PASS\n");

	{
		uint32_t result;
		if (factorial_fd(fd, 5, &result) || result != 120)
			goto fail;
	}
	if (event_readable(fd, 0) != 1 ||
	    read_events_exact(fd, events, 1)) {
		fprintf(stderr, "single-event readiness/read failed\n");
		goto fail;
	}
	if (events[0].type != EDU_EVENT_FACTORIAL || events[0].value != 120) {
		fprintf(stderr, "unexpected single event contents\n");
		goto fail;
	}
	printf("one event + poll readiness: PASS\n");

	if (issue_factorials(fd, basic_inputs,
			     sizeof(basic_inputs) / sizeof(basic_inputs[0])))
		goto fail;
	if (read_events_exact(fd, events,
			      sizeof(basic_inputs) / sizeof(basic_inputs[0])))
		goto fail;
	for (i = 0; i < sizeof(basic_inputs) / sizeof(basic_inputs[0]); i++) {
		if (events[i].type != EDU_EVENT_FACTORIAL ||
		    events[i].value != expected_factorial(basic_inputs[i])) {
			fprintf(stderr, "FIFO event payload mismatch at %zu\n", i);
			goto fail;
		}
	}
	if (check_fifo(events, sizeof(basic_inputs) / sizeof(basic_inputs[0]))) {
		fprintf(stderr, "FIFO event sequence mismatch\n");
		goto fail;
	}
	printf("FIFO order: PASS\n");

	for (i = 0; i < sizeof(wrap_inputs) / sizeof(wrap_inputs[0]); i++)
		wrap_inputs[i] = (uint32_t)(i % 6);
	if (issue_factorials(fd, wrap_inputs,
			     sizeof(wrap_inputs) / sizeof(wrap_inputs[0])) ||
	    read_events_exact(fd, events,
			      sizeof(wrap_inputs) / sizeof(wrap_inputs[0])) ||
	    check_fifo(events, sizeof(wrap_inputs) / sizeof(wrap_inputs[0]))) {
		fprintf(stderr, "ring wraparound validation failed\n");
		goto fail;
	}
	printf("ring wraparound: PASS\n");

	if (get_stats(fd, &before))
		goto fail;
	for (i = 0; i < sizeof(full_inputs) / sizeof(full_inputs[0]); i++)
		full_inputs[i] = 5;
	if (issue_factorials(fd, full_inputs,
			     sizeof(full_inputs) / sizeof(full_inputs[0])) ||
	    get_stats(fd, &after))
		goto fail;
	if (after.event_overflows - before.event_overflows != 5) {
		fprintf(stderr, "expected 5 dropped events, got %" PRIu64 "\n",
			(uint64_t)(after.event_overflows - before.event_overflows));
		goto fail;
	}
	if (read_events_exact(fd, events, EDU_EVENT_CAPACITY) ||
	    check_fifo(events, EDU_EVENT_CAPACITY)) {
		fprintf(stderr, "full-ring FIFO validation failed\n");
		goto fail;
	}
	if (events[0].sequence != before.last_sequence + 1 ||
	    events[EDU_EVENT_CAPACITY - 1].sequence !=
		before.last_sequence + EDU_EVENT_CAPACITY) {
		fprintf(stderr, "drop-new full-ring policy mismatch\n");
		goto fail;
	}
	if (event_readable(fd, 0) != 0) {
		fprintf(stderr, "ring was not empty after draining full capacity\n");
		goto fail;
	}
	printf("full condition/overflow accounting: PASS (5 dropped)\n");
	close(fd);
	return 0;

fail:
	close(fd);
	return 1;
}

static int command_events(void)
{
	struct edu_event event;
	int fd = open_device(O_RDONLY);
	int ready;
	ssize_t got;

	if (fd < 0)
		return 1;
	ready = event_readable(fd, 30000);
	if (ready <= 0) {
		fprintf(stderr, "timed out waiting for an event\n");
		close(fd);
		return 1;
	}
	got = read(fd, &event, sizeof(event));
	if (got != (ssize_t)sizeof(event)) {
		fprintf(stderr, "read event: %s\n",
			got < 0 ? strerror(errno) : "short event");
		close(fd);
		return 1;
	}
	printf("event seq=%" PRIu64 " type=%u value=%u\n",
	       (uint64_t)event.sequence, event.type, event.value);
	close(fd);
	return 0;
}

static int run_dma(int fd, uint32_t direction, void *buffer, uint32_t length)
{
	struct edu_dma_request request = {
		.direction = direction,
		.length = length,
		.user_buffer = (uint64_t)(uintptr_t)buffer,
	};

	if (ioctl(fd, EDU_IOC_DMA, &request) < 0) {
		fprintf(stderr, "DMA direction=%u length=%u: %s\n",
			direction, length, strerror(errno));
		return -1;
	}
	return 0;
}

static int command_dma_test(void)
{
	static const uint32_t lengths[] = { 1, 257, EDU_DMA_MAX_LENGTH };
	unsigned char *source;
	unsigned char *destination;
	struct edu_dma_request boundary_request;
	int fd;
	size_t size_index;
	size_t i;

	fd = open_device(O_RDWR);
	if (fd < 0)
		return 1;
	source = malloc(EDU_DMA_BUFFER_SIZE);
	destination = malloc(EDU_DMA_BUFFER_SIZE);
	if (!source || !destination) {
		fprintf(stderr, "out of memory\n");
		free(source);
		free(destination);
		close(fd);
		return 1;
	}
	boundary_request.direction = EDU_DMA_HOST_TO_DEVICE;
	boundary_request.length = EDU_DMA_BUFFER_SIZE;
	boundary_request.user_buffer = (uint64_t)(uintptr_t)source;
	errno = 0;
	if (ioctl(fd, EDU_IOC_DMA, &boundary_request) == 0 || errno != EINVAL) {
		fprintf(stderr, "4096-byte EDU boundary was not rejected safely\n");
		goto fail;
	}
	puts("4096-byte boundary safety: PASS (rejected before DMA)");

	for (size_index = 0;
	     size_index < sizeof(lengths) / sizeof(lengths[0]);
	     size_index++) {
		uint32_t length = lengths[size_index];

		for (i = 0; i < length; i++)
			source[i] = (unsigned char)((i * 37U + 0xa5U) & 0xffU);
		memset(destination, 0, length);
		if (run_dma(fd, EDU_DMA_HOST_TO_DEVICE, source, length) ||
		    run_dma(fd, EDU_DMA_DEVICE_TO_HOST, destination, length))
			goto fail;
		if (memcmp(source, destination, length)) {
			fprintf(stderr, "DMA data mismatch at length %u\n", length);
			goto fail;
		}
		printf("DMA H2D/D2H %u bytes: PASS\n", length);
	}

	printf("DMA H2D: PASS\nDMA D2H: PASS\n");
	free(source);
	free(destination);
	close(fd);
	return 0;

fail:
	free(source);
	free(destination);
	close(fd);
	return 1;
}

static int command_selftest(void)
{
	static const char *const patterns[] = {
		"0x00000000", "0xffffffff", "0x12345678", "0xa5a5a5a5"
	};
	size_t i;

	for (i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
		if (command_invert(patterns[i]))
			return 1;
	}
	if (command_factorial("5") || command_ring_test() || command_dma_test())
		return 1;
	puts("EduPCI selftest: PASS");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(stderr);
		return 2;
	}
	if (!strcmp(argv[1], "info") && argc == 2)
		return command_info();
	if (!strcmp(argv[1], "invert") && argc == 3)
		return command_invert(argv[2]);
	if (!strcmp(argv[1], "factorial") && argc == 3)
		return command_factorial(argv[2]);
	if (!strcmp(argv[1], "events") && argc == 2)
		return command_events();
	if (!strcmp(argv[1], "stats") && argc == 2)
		return command_stats();
	if (!strcmp(argv[1], "ring-test") && argc == 2)
		return command_ring_test();
	if (!strcmp(argv[1], "dma-test") && argc == 2)
		return command_dma_test();
	if (!strcmp(argv[1], "selftest") && argc == 2)
		return command_selftest();
	usage(stderr);
	return 2;
}
