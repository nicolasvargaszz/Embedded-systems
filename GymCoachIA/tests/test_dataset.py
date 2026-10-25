import csv
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dataset import Frame, Clock, parse_line, resample_segments, export, RAW_HEADER, EI_HEADER
from split_dataset import split
from evaluate_predictions import evaluate

class DatasetTests(unittest.TestCase):
    def test_serial_noise_and_invalid_data(self):
        self.assertIsNone(parse_line("# starting..."))
        self.assertIsNone(parse_line("F,2,100,0,1,2,3,4"))
        self.assertIsNone(parse_line("F,1,100,0,1,2,3"))
        self.assertTrue(parse_line("F,1,100,0,150,160,170,10").valid)
        self.assertFalse(parse_line("F,1,100,0,nan,160,170,10").valid)
        self.assertFalse(parse_line("F,1,100,0,190,160,170,10").valid)

    def test_rollover_and_reset(self):
        c=Clock(); c.unwrap(0xFFFFFFF0)
        self.assertEqual(c.unwrap(20), (1<<32)+20)
        with self.assertRaises(ValueError): c.unwrap(10)

    def test_exact_grid_and_interpolation(self):
        f=[Frame(50,0,(0,90,160,10)),Frame(170,0,(120,90,160,10)),Frame(230,0,(180,90,160,10))]
        parts=list(resample_segments(f))
        self.assertEqual(len(parts),1)
        self.assertEqual(parts[0],[(0,0,90,160,10),(100,100,90,160,10)])

    def test_never_bridge_missing_data_or_changed_side(self):
        f=[Frame(t,side,(a,90,160,10)) for t,side,a in [(0,0,20),(100,0,30),(200,0,float('nan')),(300,0,40),(400,1,50),(800,1,60)]]
        parts=list(resample_segments(f))
        self.assertEqual([len(p) for p in parts],[2,1,1,1])
        self.assertTrue(all(p[0][0]==0 for p in parts))

    def test_export_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); raw=root/'curl.p03_izq_s001_time.csv'
            with raw.open('w',newline='') as f:
                w=csv.writer(f);w.writerow(RAW_HEADER)
                for i in range(25):w.writerow([i*100,0,150,160,170,10])
            files=export(raw,root/'ei');self.assertEqual(len(files),1)
            with files[0].open() as f: rows=list(csv.reader(f))
            self.assertEqual(tuple(rows[0]),EI_HEADER); self.assertEqual(rows[-1][0],'2400')
            with self.assertRaises(FileExistsError):export(raw,root/'ei')

    def test_subject_disjoint_split(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); source=root/'source';source.mkdir()
            for person in ('p01','p07'):
                with (source/f'curl.{person}_izq_s001_time_part001.csv').open('w',newline='') as f:
                    w=csv.writer(f);w.writerow(EI_HEADER)
                    for i in range(20): w.writerow([i*100,150,160,170,10])
            records=split(source,root/'out',{'p07'})
            self.assertEqual({r['participant'] for r in records if r['subset']=='testing'},{'p07'})
            self.assertEqual({r['participant'] for r in records if r['subset']=='training'},{'p01'})
            with self.assertRaises(ValueError):split(source,root/'out',{'p07'})
            with self.assertRaises(ValueError):split(source,root/'bad',{'p99'})

    def test_f1_uses_all_four_classes(self):
        labels=('curl','sentadilla','extension','descanso')
        rows=[dict(participant='p07',truth=x,prediction=x) for x in labels]
        result=evaluate(rows); self.assertEqual(result['macro_f1'],1)
        rows[0]['prediction']='descanso'
        result=evaluate(rows);self.assertEqual(result['accuracy'],0.75)
        self.assertAlmostEqual(result['macro_f1'],2/3)

if __name__=='__main__':unittest.main()
