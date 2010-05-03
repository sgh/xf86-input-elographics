#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <X11/Xdefs.h> // For Bool
#include <QtGui/QtGui>

/* Taken from the elographics driver */
#define SHM_ELOGRAPHICS_NAME "/ELOGRAPHICS_CAL"
#define ELO_PACKET_SIZE		10

typedef struct _EloShmRec {
  int           version;          /* Magic. To ensure match with calibration software */
  int           cur_x;            /* Last raw X-coordinate                     */
  int           cur_y;            /* Last raw Y-coordinate                     */
  int           min_x;            /* Minimum x reported by calibration         */
  int           max_x;            /* Maximum x                                 */
  int           min_y;            /* Minimum y reported by calibration         */
  int           max_y;            /* Maximum y                                 */
  int           swap_axes;        /* Swap X an Y axes if != 0                  */
  int           untouch_delay;    /* Delay before reporting an untouch (in ms) */
  int           report_delay;     /* Delay between touch report packets        */
}  EloShmRec, *EloShmPtr;

int do_update_xorgconf = 0;
char xorgconf_file[256] = "/etc/X11/xorg.conf";
int be_verbose = 0;

void update_xorgconf(int minx, int maxx, int miny, int maxy) {
	char line[1024];
	char xorgconf_file_new[256];

	if (be_verbose)
		fprintf(stderr,"Updating X configration\n");
	strncpy(xorgconf_file_new, xorgconf_file, sizeof(xorgconf_file_new));
	strcat(xorgconf_file_new, ".new");
	FILE* xorgconf = fopen(xorgconf_file, "r+");
	FILE* newxorgconf = fopen(xorgconf_file_new, "w+");

	if (!xorgconf) {
		fprintf(stderr,"Unable to open %s\n", xorgconf_file);
		return;
	}

	if (!newxorgconf) {
		fclose(xorgconf);
		fprintf(stderr,"Unable to open %s\n", xorgconf_file_new);
		return;
	}

	while (fgets(line, sizeof(line), xorgconf) != NULL) {
		const char *minx_match = "#ELOGRAPHICS_MINX";
		const char *maxx_match = "#ELOGRAPHICS_MAXX";
		const char *miny_match = "#ELOGRAPHICS_MINY";
		const char *maxy_match = "#ELOGRAPHICS_MAXY";
		const char *elomatch  = NULL;
		const char *optmatch  = NULL;
		const char *optformat = NULL;
		int value;

		if (strstr(line, minx_match)) {
			elomatch  = minx_match;
			optmatch  = "MINX";
			optformat = "MinX\" \"%d\" %s\n";
			value = minx;
		}

		if (strstr(line, maxx_match)) {
			elomatch  = maxx_match;
			optmatch  = "MAXX";
			optformat = "MaxX\" \"%d\" %s\n";
			value = maxx;
		}

		if (strstr(line, miny_match)) {
			elomatch  = miny_match;
			optmatch  = "MINY";
			optformat = "MinY\" \"%d\" %s\n";
			value = miny;
		}

		if (strstr(line, maxy_match)) {
			elomatch  = maxy_match;
			optmatch  = "MAXY";
			optformat = "MaxY\" \"%d\" %s\n";
			value = maxy;
		}

		if (elomatch) {
			char tmp[1024];
			char *ptr;
			int idx = 0;

			// Uppercase the string in a temporary string
			while (line[idx]) {
				tmp[idx] = toupper(line[idx]);
				idx++;
			}
			tmp[idx] = 0;

			// Find the beginning of optionname "MinX/MaxX/MinY/MaxY"
			ptr = strstr(tmp, optmatch);

			// Write the new option-line
			if (ptr) {
				sprintf(line + (ptr-tmp), optformat, value, elomatch);
			}

		}

		fprintf(newxorgconf,"%s", line);
	}
	fclose(xorgconf);
	fclose(newxorgconf);
	rename(xorgconf_file_new, xorgconf_file);
}

class EloCalibrationWidget : public QWidget {
	int _fd;
	EloShmPtr priv;
	int _state;
	QPointF touch_points[4];
	QPointF screen_points[4];

public:
	EloCalibrationWidget(): _state(0) {
		_fd = shm_open(SHM_ELOGRAPHICS_NAME, O_RDWR, S_IRWXU);
		if (_fd<0) {
			fprintf(stderr,"Failed to open shared memory\n");
		} else {

			if ( ( priv = (EloShmRec*)mmap(NULL, sizeof(EloShmRec), PROT_READ|PROT_WRITE, MAP_SHARED, _fd, 0)) < 0 ) {
				fprintf(stderr,"Failed to map memory\n");
			}
		}

		if (_fd > 0 && be_verbose) {
			fprintf(stderr,"Current calibration :\n");
			fprintf(stderr,"MinX:%d\n",priv->min_x);
			fprintf(stderr,"MinY:%d\n",priv->min_y);
			fprintf(stderr,"MaxX:%d\n",priv->max_x);
			fprintf(stderr,"MinY:%d\n",priv->max_y);
		}
		showFullScreen();
		repaint();
	}

	void calculateMinMaxXY(void) {
		float a_x = 0;
		float b_x = 0;
		float a_y = 0;
		float b_y = 0;

		for (int i=0; i<2; i++) {
			float tmp_a;
			float tmp_b;

			// First the X axis
			tmp_a = (touch_points[1+i*2].x() - touch_points[i*2].x()) / (screen_points[1+i*2].x() - screen_points[i*2].x());
			tmp_b = touch_points[i*2].x() - tmp_a*screen_points[i*2].x();
			a_x += tmp_a;
			b_x += tmp_b;

			// First the Y axis
			tmp_a = (touch_points[2+i].y() - touch_points[i].y()) / (screen_points[2+i].y() - screen_points[i].y());
			tmp_b = touch_points[i].y() - tmp_a*screen_points[i].y();
			a_y += tmp_a;
			b_y += tmp_b;
		}

		a_x /= 2;
		b_x /= 2;
		a_y /= 2;
		b_y /= 2;

		int minx = b_x;
		int maxx = (float)rect().width()*a_x+b_x;

		int miny = b_y;
		int maxy = (float)rect().height()*a_y+b_y;

		priv->min_x = minx;
		priv->max_x = maxx;

		priv->min_y = maxy;
		priv->max_y = miny;

		if (be_verbose) {
			fprintf(stderr,"New calibration :\n");
			fprintf(stderr,"MinX:%d   MaxX:%d\n", priv->min_x, priv->max_x );
			fprintf(stderr,"MinY:%d   MaxY:%d\n", priv->min_y, priv->max_y );
		}

		if (do_update_xorgconf)
			update_xorgconf(priv->min_x, priv->max_x, priv->min_y, priv->max_y);
	}

	virtual void resizeEvent(QResizeEvent *) {
		screen_points[0] = QPointF(50,50); // Top left
		screen_points[1] = QPointF(rect().width()-50,50); // Top Right
		screen_points[2] = QPointF(50,rect().height()-50); // Bottom left
		screen_points[3] = QPointF(rect().width()-50,rect().height()-50); // bottom Right
	}

	virtual void mouseReleaseEvent ( QMouseEvent * event ) {
		switch (_state) {
			case 0: // Top left
			case 1: // Top Right
			case 2: // Bottom left
			case 3: // Bottom right
				if (_fd > 0) {
					touch_points[_state] = QPointF(priv->cur_x, priv->cur_y);
					if (be_verbose)
						fprintf(stderr,"Raw (%d,%d)\n",priv->cur_x, priv->cur_y);
				}
				_state++;
				repaint();
				break;
		}

		if (_state == 4) {
			calculateMinMaxXY();
			_state = -1;
			QApplication::exit();
		}
	}

	void paintEvent (QPaintEvent *) {
		QPainter qpainter (this);

// 		qpainter.drawRect (rect());

		int x = 0;
		int y = 0;

		switch (_state) {
			case 0: // Top left
			case 1: // Top Right
			case 2: // Bottom left
			case 3: // Bottom right
				x = screen_points[_state].x();
				y = screen_points[_state].y();
				break;
			default:
				break;
		}

		if (x!=0 && y!=0) {
			qpainter.setPen (QPen (Qt::blue, 2));
			qpainter.drawLine (x-10, y, x+10, y);
			qpainter.drawLine (x, y-10, x, y+10);
		}
	}


	~EloCalibrationWidget() {
		munmap(priv, sizeof(EloShmRec));
		shm_unlink(SHM_ELOGRAPHICS_NAME);
	}
};

int main(int argc, char* argv[]) {
	int opt;

	while ((opt = getopt(argc, argv, "hvxf:")) != -1) {
		switch (opt) {
			case 'x':
				do_update_xorgconf = 1;
				break;
			case 'f':
					strncpy(xorgconf_file, optarg, sizeof(xorgconf_file));
				break;
			case 'v':
				be_verbose = 1;
				break;
			case 'h':
			default: /* '?' */
					fprintf(stderr, "Usage: %s [-x] [-f xorg.conf]\n",argv[0]);
					fprintf(stderr, "   -x : Update xorg.conf file\n");
					fprintf(stderr, "        Option-lines must be formated like this:\n");
					fprintf(stderr, "        Option \"MinX\" \"value\" #ELOGRAPHICS_MAXX\n");
					fprintf(stderr, "   -f : Path to configuration file (default: /etc/X11/xorg.conf)\n");
					fprintf(stderr, "   -v : Be verbose during calibration\n");
					fprintf(stderr, "   -h : Show this help\n");
					exit(EXIT_FAILURE);
		}
	}

	QApplication app(argc, argv);
	EloCalibrationWidget ecal;
	
	return app.exec();
}
